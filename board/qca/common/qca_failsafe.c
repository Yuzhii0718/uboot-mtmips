// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Yuzhii0718 <admin@yuzhii0718.eu.org>
 *
 * Failsafe operations for QCA/Atheros boards.
 *
 * Architecture:
 *   - NOR flash (default): erase whole mtd partition, then write
 *   - NAND flash (CONFIG_QCA_FAILSAFE_NAND): bad-block-aware erase/write
 *
 * This file provides the non-weak overrides for the generic failsafe
 * web UI in failsafe/failsafe.c.
 */

#include <command.h>
#include <errno.h>
#include <image.h>
#include <linux/mtd/mtd.h>
#include <mtd.h>
#include <spi_flash.h>

/* QCA partition names — must match CONFIG_MTDPARTS_DEFAULT */
#define QCA_FIRMWARE_PART	"firmware"
#define QCA_UBOOT_PART		"u-boot"

/* ------------------------------------------------------------------ */

static int qca_mtd_setup(void)
{
	static int done;

	if (done)
		return 0;

	run_command("sf probe", 0);
	run_command("mtdparts default", 0);
	mtd_probe_devices();
	done = 1;
	return 0;
}

/* ------------------------------------------------------------------ */
/* NOR: whole-partition erase + write (simple, fast, no bad blocks)   */
/* ------------------------------------------------------------------ */

static int nor_erase_write(const char *part_name,
			   const void *data, size_t size)
{
	struct mtd_info *mtd;
	struct erase_info ei;
	int ret;

	qca_mtd_setup();

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
/* NAND: bad-block-aware erase + write                                */
/* ------------------------------------------------------------------ */

static int nand_erase_write(const char *part_name,
			    const void *data, size_t size)
{
	/* TODO: implement when QCA NAND boards are available.
	 *
	 * The NAND path will:
	 *   1. mtd_block_isbad() check before each block erase
	 *   2. Skip bad blocks, use next good block
	 *   3. mtd_write_oob() for ECC-protected write
	 *   4. Track erased/written size for alignment
	 */
	printf("Error: NAND failsafe not yet implemented\n");
	return -ENOSYS;
}

/* ------------------------------------------------------------------ */
/* Dispatch: NOR or NAND based on CONFIG_QCA_FAILSAFE_NAND             */
/* ------------------------------------------------------------------ */

static int qca_mtd_erase_write(const char *part_name,
			       const void *data, size_t size)
{
	if (IS_ENABLED(CONFIG_QCA_FAILSAFE_NAND))
		return nand_erase_write(part_name, data, size);

	return nor_erase_write(part_name, data, size);
}

/* ------------------------------------------------------------------ */
/* Weak overrides called by failsafe/failsafe.c                        */
/* ------------------------------------------------------------------ */

/**
 * httpd_get_upload_buffer_ptr() - provide a safe RAM buffer for uploads
 *
 * Returns a pointer to a region of RAM that is safe to use for
 * receiving uploaded firmware data via HTTP.
 */
void *httpd_get_upload_buffer_ptr(size_t size)
{
	/* Use SYS_LOAD_ADDR area; on QCA this is typically at 0x80060000
	 * with plenty of DRAM above it.
	 */
	return (void *)CONFIG_SYS_LOAD_ADDR;
}

/**
 * failsafe_validate_image() - validate a firmware image before writing
 *
 * Checks that the uploaded data is a valid uImage or FIT image.
 * Returns 0 on success, negative on error.
 */
int failsafe_validate_image(const void *data, size_t size)
{
	if (!data || size < sizeof(struct legacy_img_hdr)) {
		printf("Error: firmware image too small (%zu bytes)\n", size);
		return -EINVAL;
	}

	/* Accept legacy uImage */
	if (image_check_magic(data))
		return 0;

	/* Accept FIT image (FDT magic) */
	if (genimg_get_format(data) == IMAGE_FORMAT_FIT)
		return 0;

	printf("Error: invalid firmware image format\n");
	return -EINVAL;
}

/**
 * failsafe_write_image() - write firmware image to flash
 *
 * Erases the "firmware" MTD partition and writes the new image.
 */
int failsafe_write_image(const void *data, size_t size)
{
	printf("\n*** Upgrading firmware (%zu bytes) ***\n", size);
	return qca_mtd_erase_write(QCA_FIRMWARE_PART, data, size);
}

/**
 * failsafe_validate_uboot() - validate a U-Boot image before writing
 *
 * Basic sanity check for the bootloader binary.
 * Returns 0 on success, negative on error.
 */
int failsafe_validate_uboot(const void *data, size_t size)
{
	if (!data || size < 64) {
		printf("Error: bootloader image too small (%zu bytes)\n",
		       size);
		return -EINVAL;
	}

	/* Basic check: U-Boot binary should be at least 64KB for MIPS */
	if (size < 0x10000) {
		printf("Warning: bootloader size (%zu) seems too small\n",
		       size);
	}

	return 0;
}

/**
 * failsafe_write_uboot() - write bootloader image to flash
 *
 * Erases the "u-boot" MTD partition and writes the new bootloader.
 * WARNING: A failed bootloader write may brick the device.
 */
int failsafe_write_uboot(const void *data, size_t size)
{
	printf("\n*** Upgrading U-Boot (%zu bytes) ***\n", size);
	printf("*** WARNING: Do not power off during write! ***\n\n");
	return qca_mtd_erase_write(QCA_UBOOT_PART, data, size);
}
