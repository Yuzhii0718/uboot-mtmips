// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Yuzhii0718 <admin@yuzhii0718.eu.org>
 *
 * Legacy failsafe operations for QCA/Atheros boards.
 *
 * In the legacy partition scheme (rootfs + uImage), the firmware image
 * is a combined rootfs (squashfs) + uImage (kernel).  This file handles
 * writing the combined image across two separate MTD partitions.
 *
 * Partitions (e.g. AP143):
 *   u-boot | u-boot-env | rootfs | uImage | ART
 */

#include <command.h>
#include <errno.h>
#include <image.h>
#include <linux/mtd/mtd.h>
#include <mtd.h>

#define QCA_LEGACY_ROOTFS_PART	"rootfs"
#define QCA_LEGACY_UIMAGE_PART	"uImage"
#define QCA_LEGACY_UBOOT_PART	"u-boot"
#define QCA_LEGACY_ART_PART	"ART"
#define QCA_LEGACY_ART_PART_ALT	"art"

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
/* Erase + write a single MTD partition (NOR)                          */
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

/* ------------------------------------------------------------------ */
/* Erase + write named partition (NOR)                                 */
/* ------------------------------------------------------------------ */

static int nor_erase_write(const char *part_name,
			   const void *data, size_t size)
{
	struct mtd_info *mtd;
	int ret;

	qca_mtd_setup();

	mtd = get_mtd_device_nm(part_name);
	if (IS_ERR_OR_NULL(mtd)) {
		printf("Error: MTD partition '%s' not found\n", part_name);
		return -ENODEV;
	}

	ret = nor_erase_write_part(mtd, data, size);
	put_mtd_device(mtd);

	return ret;
}

/* ------------------------------------------------------------------ */
/* Write combined firmware to rootfs + uImage partitions               */
/* ------------------------------------------------------------------ */

static int legacy_write_firmware(const void *data, size_t size)
{
	struct mtd_info *mtd_rootfs, *mtd_uimage;
	size_t rootfs_size, uimage_size;
	int ret;

	qca_mtd_setup();

	mtd_rootfs = get_mtd_device_nm(QCA_LEGACY_ROOTFS_PART);
	if (IS_ERR_OR_NULL(mtd_rootfs)) {
		printf("Error: MTD partition '%s' not found\n",
		       QCA_LEGACY_ROOTFS_PART);
		return -ENODEV;
	}

	mtd_uimage = get_mtd_device_nm(QCA_LEGACY_UIMAGE_PART);
	if (IS_ERR_OR_NULL(mtd_uimage)) {
		printf("Error: MTD partition '%s' not found\n",
		       QCA_LEGACY_UIMAGE_PART);
		put_mtd_device(mtd_rootfs);
		return -ENODEV;
	}

	rootfs_size = mtd_rootfs->size;
	uimage_size = mtd_uimage->size;

	printf("Partition '%s': %llu bytes\n"
	       "Partition '%s': %llu bytes\n"
	       "Firmware image: %zu bytes\n",
	       QCA_LEGACY_ROOTFS_PART, mtd_rootfs->size,
	       QCA_LEGACY_UIMAGE_PART, mtd_uimage->size,
	       size);

	if (size > rootfs_size + uimage_size) {
		printf("Error: firmware image too large (%zu > %llu)\n",
		       size, (unsigned long long)(rootfs_size + uimage_size));
		put_mtd_device(mtd_rootfs);
		put_mtd_device(mtd_uimage);
		return -ENOSPC;
	}

	/* Write rootfs portion */
	{
		size_t wsize = size < rootfs_size ? size : rootfs_size;

		printf("\n--- Writing rootfs portion (%zu bytes) ---\n",
		       wsize);
		ret = nor_erase_write_part(mtd_rootfs, data, wsize);
		if (ret)
			goto out;
	}

	/* Write uImage portion if there is remaining data */
	if (size > rootfs_size) {
		size_t wsize = size - rootfs_size;

		if (wsize > uimage_size)
			wsize = uimage_size;

		printf("\n--- Writing uImage portion (%zu bytes) ---\n",
		       wsize);
		ret = nor_erase_write_part(mtd_uimage,
					   data + rootfs_size, wsize);
		if (ret)
			goto out;
	}

	printf("\n*** Firmware upgrade completed ***\n");

out:
	put_mtd_device(mtd_rootfs);
	put_mtd_device(mtd_uimage);

	return ret;
}

/* ------------------------------------------------------------------ */
/* Weak overrides called by failsafe/failsafe.c                        */
/* ------------------------------------------------------------------ */

void *httpd_get_upload_buffer_ptr(size_t size)
{
	return (void *)CONFIG_SYS_LOAD_ADDR;
}

int failsafe_validate_image(const void *data, size_t size)
{
	if (!data || size < sizeof(struct legacy_img_hdr)) {
		printf("Error: firmware image too small (%zu bytes)\n", size);
		return -EINVAL;
	}

	/* For legacy combined rootfs+uImage, check that the uImage portion
	 * (at the end of the image) is a valid image.
	 */
	if (image_check_magic(data))
		return 0;

	if (genimg_get_format(data) == IMAGE_FORMAT_FIT)
		return 0;

	/* If the start of the image isn't a kernel, the image might be
	 * rootfs+uImage combined.  Try to locate the uImage header within
	 * a reasonable search window at the tail of the image.
	 */
	{
		const u8 *p;
		size_t off;

		for (off = size > 0x200000 ? size - 0x200000 : 0;
		     off + sizeof(struct legacy_img_hdr) <= size;
		     off += 4) {
			p = (const u8 *)data + off;
			if (image_check_magic((const struct legacy_img_hdr *)p) ||
			    genimg_get_format(p) == IMAGE_FORMAT_FIT)
				return 0;
		}
	}

	printf("Error: invalid firmware image format\n");
	return -EINVAL;
}

int failsafe_write_image(const void *data, size_t size)
{
	printf("\n*** Upgrading firmware (%zu bytes) ***\n", size);
	return legacy_write_firmware(data, size);
}

int failsafe_validate_uboot(const void *data, size_t size)
{
	if (!data || size < 64) {
		printf("Error: bootloader image too small (%zu bytes)\n",
		       size);
		return -EINVAL;
	}

	if (size < 0x10000)
		printf("Warning: bootloader size (%zu) seems too small\n",
		       size);

	return 0;
}

int failsafe_write_uboot(const void *data, size_t size)
{
	printf("\n*** Upgrading U-Boot (%zu bytes) ***\n", size);
	printf("*** WARNING: Do not power off during write! ***\n\n");
	return nor_erase_write(QCA_LEGACY_UBOOT_PART, data, size);
}

/* ------------------------------------------------------------------ */
/* RF Calibration (ART partition)                                      */
/* ------------------------------------------------------------------ */

int failsafe_validate_art(const void *data, size_t size)
{
	const char *part_names[] = { QCA_LEGACY_ART_PART, QCA_LEGACY_ART_PART_ALT };
	struct mtd_info *mtd = NULL;
	int i;

	if (!data || !size) {
		printf("Error: RF calibration data is empty\n");
		return -EINVAL;
	}

	qca_mtd_setup();

	for (i = 0; i < ARRAY_SIZE(part_names); i++) {
		mtd = get_mtd_device_nm(part_names[i]);
		if (!IS_ERR_OR_NULL(mtd))
			break;
	}

	if (IS_ERR_OR_NULL(mtd)) {
		printf("Error: ART partition not found"
		       " (tried '%s', '%s')\n",
		       QCA_LEGACY_ART_PART, QCA_LEGACY_ART_PART_ALT);
		return -ENODEV;
	}

	if (size > mtd->size) {
		printf("Error: RF calibration data (%zu bytes)"
		       " exceeds partition size (%llu bytes)\n",
		       size, mtd->size);
		put_mtd_device(mtd);
		return -EFBIG;
	}

	put_mtd_device(mtd);
	return 0;
}

int failsafe_write_art(const void *data, size_t size)
{
	const char *part_names[] = { QCA_LEGACY_ART_PART, QCA_LEGACY_ART_PART_ALT };
	struct mtd_info *mtd = NULL;
	int i;

	qca_mtd_setup();

	for (i = 0; i < ARRAY_SIZE(part_names); i++) {
		mtd = get_mtd_device_nm(part_names[i]);
		if (!IS_ERR_OR_NULL(mtd))
			break;
	}

	if (IS_ERR_OR_NULL(mtd)) {
		printf("Error: ART partition not found"
		       " (tried '%s', '%s')\n",
		       QCA_LEGACY_ART_PART, QCA_LEGACY_ART_PART_ALT);
		return -ENODEV;
	}

	printf("\n*** Upgrading ART calibration (%zu bytes) ***\n", size);
	printf("*** WARNING: Bad calibration data can break WiFi! ***\n\n");

	return nor_erase_write_part(mtd, data, size);
}
