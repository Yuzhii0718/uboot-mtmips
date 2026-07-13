// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 *
 * Failsafe operations
 */

#include <command.h>
#include <errno.h>
#include <linux/kernel.h>
#include <linux/mtd/mtd.h>
#include <linux/string.h>
#include <mtd.h>
#include <asm/global_data.h>
#include "upgrade_helper.h"
#include "colored_print.h"

DECLARE_GLOBAL_DATA_PTR;

#define UPGRADE_PART		"fw"
#define UPGRADE_UBOOT_PART	"bl"

static const struct data_part_entry *find_part(const struct data_part_entry *parts,
					       u32 num_parts, const char *abbr)
{
	u32 i;

	if (!abbr)
		return NULL;

	for (i = 0; i < num_parts; i++) {
		if (!strcmp(parts[i].abbr, abbr))
			return &parts[i];
	}

	cprintln(ERROR, "*** Invalid upgrading part! ***");

	return NULL;
}

void *httpd_get_upload_buffer_ptr(size_t size)
{
	/* Skip BL31 address range started from 0x43000000 */
	return (void *)gd->ram_base + 0x4000000;
}

int failsafe_validate_image(const void *data, size_t size)
{
	const struct data_part_entry *upgrade_parts, *dpe;
	u32 num_parts;

	board_upgrade_data_parts(&upgrade_parts, &num_parts);

	if (!upgrade_parts || !num_parts) {
		printf("mtkupgrade is not configured!\n");
		return -ENOSYS;
	}

	dpe = find_part(upgrade_parts, num_parts, UPGRADE_PART);
	if (!dpe)
		return -ENODEV;

	if (dpe->validate)
		return dpe->validate(dpe->priv, dpe, data, size);

	return 0;
}

int failsafe_write_image(const void *data, size_t size)
{
	const struct data_part_entry *upgrade_parts, *dpe;
	u32 num_parts;
	int ret;

	board_upgrade_data_parts(&upgrade_parts, &num_parts);

	if (!upgrade_parts || !num_parts) {
		printf("mtkupgrade is not configured!\n");
		return -ENOSYS;
	}

	dpe = find_part(upgrade_parts, num_parts, UPGRADE_PART);
	if (!dpe)
		return -ENODEV;

	printf("\n");
	cprintln(PROMPT, "*** Upgrading %s ***", dpe->name);
	cprintln(PROMPT, "*** Data: %zd (0x%zx) bytes at 0x%08lx ***",
		 size, size, (ulong)data);
	printf("\n");

	ret = dpe->write(dpe->priv, dpe, data, size);
	if (ret)
		return ret;

	printf("\n");
	cprintln(PROMPT, "*** %s upgrade completed! ***", dpe->name);
	printf("\n");

	if (dpe->do_post_action)
		dpe->do_post_action(dpe->priv, dpe, data, size);

	return 0;
}

int failsafe_validate_uboot(const void *data, size_t size)
{
	const struct data_part_entry *upgrade_parts, *dpe;
	u32 num_parts;

	board_upgrade_data_parts(&upgrade_parts, &num_parts);

	if (!upgrade_parts || !num_parts) {
		printf("mtkupgrade is not configured!\n");
		return -ENOSYS;
	}

	dpe = find_part(upgrade_parts, num_parts, UPGRADE_UBOOT_PART);
	if (!dpe)
		return -ENODEV;

	if (dpe->validate)
		return dpe->validate(dpe->priv, dpe, data, size);

	return 0;
}

int failsafe_write_uboot(const void *data, size_t size)
{
	const struct data_part_entry *upgrade_parts, *dpe;
	u32 num_parts;
	int ret;

	board_upgrade_data_parts(&upgrade_parts, &num_parts);

	if (!upgrade_parts || !num_parts) {
		printf("mtkupgrade is not configured!\n");
		return -ENOSYS;
	}

	dpe = find_part(upgrade_parts, num_parts, UPGRADE_UBOOT_PART);
	if (!dpe)
		return -ENODEV;

	printf("\n");
	cprintln(PROMPT, "*** Upgrading %s ***", dpe->name);
	cprintln(PROMPT, "*** Data: %zd (0x%zx) bytes at 0x%08lx ***",
		 size, size, (ulong)data);
	printf("\n");

	ret = dpe->write(dpe->priv, dpe, data, size);
	if (ret)
		return ret;

	printf("\n");
	cprintln(PROMPT, "*** %s upgrade completed! ***", dpe->name);
	printf("\n");

	if (dpe->do_post_action)
		dpe->do_post_action(dpe->priv, dpe, data, size);

	return 0;
}

/* ---------------------------------------------------------------- */
/* RF Calibration (Factory partition)                                */
/* ---------------------------------------------------------------- */

#define MTK_FACTORY_PART	"Factory"
#define MTK_FACTORY_PART_ALT	"factory"

int failsafe_validate_art(const void *data, size_t size)
{
	const char *part_names[] = { MTK_FACTORY_PART, MTK_FACTORY_PART_ALT };
	struct mtd_info *mtd = NULL;
	int i;

	if (!data || !size) {
		printf("Error: RF calibration data is empty\n");
		return -EINVAL;
	}

	mtd_probe_devices();

	for (i = 0; i < ARRAY_SIZE(part_names); i++) {
		mtd = get_mtd_device_nm(part_names[i]);
		if (!IS_ERR_OR_NULL(mtd))
			break;
	}

	if (IS_ERR_OR_NULL(mtd)) {
		printf("Error: Factory partition not found"
		       " (tried '%s', '%s')\n",
		       MTK_FACTORY_PART, MTK_FACTORY_PART_ALT);
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
	/* Try "Factory" first, then "factory" */
	const char *part_names[] = { MTK_FACTORY_PART, MTK_FACTORY_PART_ALT };
	struct mtd_info *mtd = NULL;
	int i;

	mtd_probe_devices();

	for (i = 0; i < ARRAY_SIZE(part_names); i++) {
		mtd = get_mtd_device_nm(part_names[i]);
		if (!IS_ERR_OR_NULL(mtd))
			break;
	}

	if (IS_ERR_OR_NULL(mtd)) {
		printf("Error: Factory partition not found"
		       " (tried '%s', '%s')\n",
		       MTK_FACTORY_PART, MTK_FACTORY_PART_ALT);
		return -ENODEV;
	}

	if (size > mtd->size) {
		printf("Error: RF calibration data (%zu bytes)"
		       " exceeds partition size (%llu bytes)\n",
		       size, mtd->size);
		put_mtd_device(mtd);
		return -ENOSPC;
	}

	printf("\n");
	cprintln(PROMPT, "*** Upgrading RF Calibration (%s) ***", mtd->name);
	cprintln(PROMPT, "*** Data: %zd (0x%zx) bytes ***", size, size);
	printf("\n");

	{
		struct erase_info ei;
		int ret;

		printf("Erasing '%s' (0x%llx bytes) ... ", mtd->name, mtd->size);
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

		printf("Writing %zu bytes to '%s' ... ", size, mtd->name);
		ret = mtd_write(mtd, 0, size, NULL, (const u_char *)data);
		if (ret) {
			printf("FAILED (err=%d)\n", ret);
			put_mtd_device(mtd);
			return ret;
		}
		printf("OK\n");
	}

	printf("\n");
	cprintln(PROMPT, "*** RF Calibration upgrade completed! ***");
	printf("\n");

	put_mtd_device(mtd);
	return 0;
}
