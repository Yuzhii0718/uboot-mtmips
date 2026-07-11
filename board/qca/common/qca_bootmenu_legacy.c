// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Yuzhii0718 <admin@yuzhii0718.eu.org>
 *
 * QCA legacy bootmenu — rootfs + uImage partition layout.
 *
 * Provides:
 *   board_bootmenu_entries() — boot menu items for legacy QCA platforms
 *   board_boot_default()     — boot uImage from uImage partition
 *
 * Legacy partition scheme (e.g. AP143):
 *   u-boot | u-boot-env | rootfs | uImage | ART
 *
 * The firmware image combines rootfs (squashfs) + uImage (kernel).
 * Booting reads uImage from flash into RAM and runs bootm.
 */

#include <command.h>
#include <env.h>
#include <image.h>
#include <mtd.h>
#include <linux/mtd/mtd.h>

#include "autoboot_helper.h"

#define QCA_LEGACY_ROOTFS_PART	"rootfs"
#define QCA_LEGACY_UIMAGE_PART	"uImage"

int board_boot_default(bool do_boot)
{
	struct mtd_info *mtd;
	void *load_addr;
	size_t rd;
	int ret;

	if (!do_boot)
		return 0;

	/* Initialize flash and partitions */
	run_command("sf probe", 0);
	run_command("mtdparts default", 0);
	mtd_probe_devices();

	mtd = get_mtd_device_nm(QCA_LEGACY_UIMAGE_PART);
	if (IS_ERR_OR_NULL(mtd)) {
		printf("ERROR: '%s' partition not found\n",
		       QCA_LEGACY_UIMAGE_PART);
		return -ENODEV;
	}

	load_addr = (void *)CONFIG_SYS_LOAD_ADDR;

	printf("Loading uImage from '%s' (0x%llx, %llu bytes) ...\n",
	       QCA_LEGACY_UIMAGE_PART, mtd->offset, mtd->size);

	/* Read uImage from flash into RAM */
	ret = mtd_read(mtd, 0, mtd->size, &rd, (u_char *)load_addr);
	put_mtd_device(mtd);

	if (ret) {
		printf("ERROR: failed to read uImage (err=%d)\n", ret);
		return ret;
	}

	printf("Booting uImage at 0x%lx (%zu bytes read)\n",
	       (ulong)load_addr, rd);

	ret = run_command("bootm $loadaddr", 0);
	if (ret)
		printf("ERROR: boot failed (err=%d)\n", ret);

	return ret;
}

static const struct bootmenu_entry qca_legacy_bootmenu_entries[] = {
	{
		.desc = "Startup system (Default)",
		.cmd = "qcaboardboot",
	},
	{
		.desc = "Upgrade firmware",
		.cmd = "qcaupgrade fw",
	},
	{
		.desc = "Upgrade bootloader",
		.cmd = "qcaupgrade bl",
	},
#ifdef CONFIG_WEBUI_FAILSAFE
	{
		.desc = "Start Web failsafe",
		.cmd = "httpd",
	},
#endif
};

void board_bootmenu_entries(const struct bootmenu_entry **menu, u32 *count)
{
	*menu = qca_legacy_bootmenu_entries;
	*count = ARRAY_SIZE(qca_legacy_bootmenu_entries);
}
