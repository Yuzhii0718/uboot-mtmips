// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Yuzhii0718 <admin@yuzhii0718.eu.org>
 *
 * QCA firmware/bootloader upgrade command via TFTP + MTD.
 *
 * Usage:
 *   qcaupgrade fw   — TFTP download firmware.bin, write to "firmware" mtd
 *   qcaupgrade bl   — TFTP download uboot.bin,  write to "u-boot" mtd
 */

#include <command.h>
#include <env.h>
#include <errno.h>
#include <mtd.h>
#include <linux/mtd/mtd.h>
#include <image.h>
#include <vsprintf.h>

/* QCA partition names — must match CONFIG_MTDPARTS_DEFAULT */
#ifdef CONFIG_QCA_BOOTMENU_LEGACY
#define QCA_FIRMWARE_PART	"rootfs"
#define QCA_UIMAGE_PART		"uImage"
#else
#define QCA_FIRMWARE_PART	"firmware"
#endif
#define QCA_UBOOT_PART		"u-boot"

static const char *default_fw_file = "firmware.bin";
static const char *default_bl_file = "uboot.bin";

/* ------------------------------------------------------------------ */
/* MTD erase + write helper (NOR)                                      */
/* ------------------------------------------------------------------ */

static int nor_erase_write_part(struct mtd_info *mtd,
				const void *data, size_t size)
{
	struct erase_info ei;
	int ret;

	if (size > mtd->size) {
		printf("Error: data (%zu bytes) exceeds partition size"
		       " (%llu bytes)\n", size, mtd->size);
		return -ENOSPC;
	}

	printf("Erasing '%s' (0x%llx bytes) ... ", mtd->name, mtd->size);
	memset(&ei, 0, sizeof(ei));
	ei.mtd = mtd;
	ei.addr = 0;
	ei.len = mtd->size;
	ret = mtd_erase(mtd, &ei);
	if (ret) {
		printf("FAILED (err=%d)\n", ret);
		return ret;
	}
	printf("OK\n");

	printf("Writing %zu bytes to '%s' ... ", size, mtd->name);
	ret = mtd_write(mtd, 0, size, NULL, (const u_char *)data);
	if (ret) {
		printf("FAILED (err=%d)\n", ret);
		return ret;
	}
	printf("OK\n");

	return 0;
}

static int nor_erase_write(const char *part_name,
			   const void *data, size_t size)
{
	struct mtd_info *mtd;
	int ret;

	mtd = get_mtd_device_nm(part_name);
	if (IS_ERR_OR_NULL(mtd)) {
		printf("Error: MTD partition '%s' not found\n", part_name);
		return -ENODEV;
	}

	ret = nor_erase_write_part(mtd, data, size);
	put_mtd_device(mtd);

	return ret;
}

#ifdef CONFIG_QCA_BOOTMENU_LEGACY
/*
 * Legacy write: split the combined rootfs+uImage across two partitions.
 * The image layout is [rootfs data][uImage data], matching the flash
 * layout: rootfs partition followed by uImage partition.
 */
static int legacy_erase_write_firmware(const void *data, size_t size)
{
	struct mtd_info *mtd_rootfs, *mtd_uimage;
	int ret;

	mtd_rootfs = get_mtd_device_nm(QCA_FIRMWARE_PART);
	if (IS_ERR_OR_NULL(mtd_rootfs)) {
		printf("Error: MTD partition '%s' not found\n",
		       QCA_FIRMWARE_PART);
		return -ENODEV;
	}

	mtd_uimage = get_mtd_device_nm(QCA_UIMAGE_PART);
	if (IS_ERR_OR_NULL(mtd_uimage)) {
		printf("Error: MTD partition '%s' not found\n",
		       QCA_UIMAGE_PART);
		put_mtd_device(mtd_rootfs);
		return -ENODEV;
	}

	if (size > mtd_rootfs->size + mtd_uimage->size) {
		printf("Error: firmware too large (%zu > %llu + %llu)\n",
		       size, mtd_rootfs->size, mtd_uimage->size);
		put_mtd_device(mtd_rootfs);
		put_mtd_device(mtd_uimage);
		return -ENOSPC;
	}

	printf("Partition '%s': %llu bytes, '%s': %llu bytes\n",
	       QCA_FIRMWARE_PART, mtd_rootfs->size,
	       QCA_UIMAGE_PART, mtd_uimage->size);

	/* Write rootfs portion */
	{
		size_t wsize = size < mtd_rootfs->size ?
			       size : mtd_rootfs->size;

		printf("\n--- Writing rootfs portion (%zu bytes) ---\n",
		       wsize);
		ret = nor_erase_write_part(mtd_rootfs, data, wsize);
		if (ret)
			goto out;
	}

	/* Write uImage portion */
	if (size > mtd_rootfs->size) {
		size_t wsize = size - mtd_rootfs->size;

		if (wsize > mtd_uimage->size)
			wsize = mtd_uimage->size;

		printf("\n--- Writing uImage portion (%zu bytes) ---\n",
		       wsize);
		ret = nor_erase_write_part(mtd_uimage,
					   data + mtd_rootfs->size, wsize);
		if (ret)
			goto out;
	}

	ret = 0;
	printf("\n=== Legacy firmware upgrade complete ===\n");

out:
	put_mtd_device(mtd_rootfs);
	put_mtd_device(mtd_uimage);
	return ret;
}
#endif /* CONFIG_QCA_BOOTMENU_LEGACY */

/* ------------------------------------------------------------------ */
/* Validate image before writing                                       */
/* ------------------------------------------------------------------ */

static int validate_image(const void *data, size_t size)
{
	if (!data || size < sizeof(struct legacy_img_hdr)) {
		printf("Error: image too small (%zu bytes)\n", size);
		return -EINVAL;
	}

	if (image_check_magic(data))
		return 0;

	if (genimg_get_format(data) == IMAGE_FORMAT_FIT)
		return 0;

	printf("Error: invalid image format\n");
	return -EINVAL;
}

static int validate_uboot(const void *data, size_t size)
{
	if (!data || size < 0x10000) {
		printf("Error: bootloader image too small (%zu bytes,"
		       " min 64KB)\n", size);
		return -EINVAL;
	}

	/* Basic sanity: U-Boot binary should be at least 64KB */
	return 0;
}

/* ------------------------------------------------------------------ */
/* Upgrade command handler                                             */
/* ------------------------------------------------------------------ */

static int do_qcaupgrade(struct cmd_tbl *cmdtp, int flag, int argc,
			 char *const argv[])
{
	const char *target;
	const char *file;
	const char *part;
	void *load_addr;
	size_t size;
	char cmd_buf[256];
	int ret;

	if (argc < 2) {
		printf("Usage: qcaupgrade fw|bl [filename]\n");
		return CMD_RET_USAGE;
	}

	target = argv[1];

	if (argc >= 3)
		file = argv[2];
	else if (!strcmp(target, "fw"))
		file = env_get("upgrade_fw_file") ? : default_fw_file;
	else
		file = env_get("upgrade_bl_file") ? : default_bl_file;

	load_addr = (void *)CONFIG_SYS_LOAD_ADDR;

	/* Step 1: TFTP download via standard tftpboot command */
	printf("\n=== TFTP downloading '%s' to 0x%p ===\n",
	       file, load_addr);

	snprintf(cmd_buf, sizeof(cmd_buf), "tftpboot %lx %s",
		 (unsigned long)load_addr, file);
	ret = run_command(cmd_buf, 0);
	if (ret) {
		printf("Error: TFTP download failed (err=%d)\n", ret);
		return CMD_RET_FAILURE;
	}

	size = env_get_hex("filesize", 0);
	if (size == 0) {
		printf("Error: downloaded file size is zero\n");
		return CMD_RET_FAILURE;
	}
	printf("Downloaded %zu bytes\n", size);

	/* Step 2: Validate */
	if (!strcmp(target, "fw")) {
		ret = validate_image(load_addr, size);
#ifdef CONFIG_QCA_BOOTMENU_LEGACY
		part = QCA_FIRMWARE_PART; /* rootfs */
#else
		part = QCA_FIRMWARE_PART;
#endif
	} else if (!strcmp(target, "bl")) {
		ret = validate_uboot(load_addr, size);
		part = QCA_UBOOT_PART;
	} else {
		printf("Error: unknown target '%s' (use fw or bl)\n",
		       target);
		return CMD_RET_USAGE;
	}

	if (ret) {
		printf("Error: image validation failed\n");
		return CMD_RET_FAILURE;
	}

	/* Step 3: Flash init */
	run_command("sf probe", 0);
	run_command("mtdparts default", 0);
	mtd_probe_devices();

	/* Step 4: Write to flash */
	printf("\n=== Writing %s (%zu bytes) ===\n", file, size);

	if (!strcmp(target, "bl"))
		printf("*** WARNING: Do not power off during write! ***\n");

#ifdef CONFIG_QCA_BOOTMENU_LEGACY
	if (!strcmp(target, "fw"))
		ret = legacy_erase_write_firmware(load_addr, size);
	else
		ret = nor_erase_write(part, load_addr, size);
#else
	ret = nor_erase_write(part, load_addr, size);
#endif
	if (ret) {
		printf("Error: write failed (err=%d)\n", ret);
		return CMD_RET_FAILURE;
	}

	printf("\n=== Upgrade complete ===\n");
	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(qcaupgrade, 3, 0, do_qcaupgrade,
	   "Upgrade firmware/bootloader via TFTP",
	   "fw [filename]   - upgrade firmware\n"
	   "qcaupgrade bl [filename]   - upgrade bootloader\n"
);
