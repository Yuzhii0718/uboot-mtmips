// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Yuzhii0718 <admin@yuzhii0718.eu.org>
 *
 * QCA bootmenu entries and default boot implementation.
 *
 * Provides:
 *   board_bootmenu_entries() — boot menu items for QCA platforms
 *   board_boot_default()     — default OS boot path
 */

#include <command.h>
#include <env.h>
#include <mtd.h>
#include <linux/mtd/mtd.h>

#include "autoboot_helper.h"

/* Default boot command — run the standard bootcmd */
int board_boot_default(bool do_boot)
{
	int ret = 0;

	if (!do_boot)
		return 0;

	/* Initialize flash and partitions */
	run_command("sf probe", 0);
	run_command("mtdparts default", 0);

	/* Run the default boot command from environment */
	ret = run_command("run bootcmd", 0);
	if (ret)
		printf("ERROR: boot failed (err=%d)\n", ret);

	return ret;
}

static const struct bootmenu_entry qca_bootmenu_entries[] = {
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
	{
		.desc = "Load image (TFTP)",
		.cmd = "tftpboot $loadaddr",
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
	*menu = qca_bootmenu_entries;
	*count = ARRAY_SIZE(qca_bootmenu_entries);
}
