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
#define QCA_FIRMWARE_PART	"firmware"
#define QCA_UBOOT_PART		"u-boot"

static const char *default_fw_file = "firmware.bin";
static const char *default_bl_file = "uboot.bin";

/* ------------------------------------------------------------------ */
/* MTD erase + write helper (NOR)                                      */
/* ------------------------------------------------------------------ */

static int nor_erase_write(const char *part_name,
			   const void *data, size_t size)
{
	struct mtd_info *mtd;
	struct erase_info ei;
	int ret;

	mtd = get_mtd_device_nm(part_name);
	if (IS_ERR_OR_NULL(mtd)) {
		printf("Error: MTD partition '%s' not found\n", part_name);
		return -ENODEV;
	}

	if (size > mtd->size) {
		printf("Error: data (%zu bytes) exceeds partition size"
		       " (%llu bytes)\n", size, mtd->size);
		put_mtd_device(mtd);
		return -ENOSPC;
	}

	printf("Erasing '%s' (0x%llx bytes) ... ", part_name, mtd->size);
	memset(&ei, 0, sizeof(ei));
	ei.mtd = mtd;
	ei.addr = 0;
	ei.len = mtd->size;
	ret = mtd_erase(mtd, &ei);
	if (ret) {
		printf("FAILED (err=%d)\n", ret);
		put_mtd_device(mtd);
		return ret;
	}
	printf("OK\n");

	printf("Writing %zu bytes to '%s' ... ", size, part_name);
	ret = mtd_write(mtd, 0, size, NULL, (const u_char *)data);
	if (ret) {
		printf("FAILED (err=%d)\n", ret);
		put_mtd_device(mtd);
		return ret;
	}
	printf("OK\n");

	put_mtd_device(mtd);
	return 0;
}

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
		part = QCA_FIRMWARE_PART;
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

	/* Step 4: Write to flash */
	printf("\n=== Writing %s (%zu bytes) to '%s' ===\n",
	       file, size, part);

	if (!strcmp(target, "bl"))
		printf("*** WARNING: Do not power off during write! ***\n");

	ret = nor_erase_write(part, load_addr, size);
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
