// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Yuzhii0718 <admin@yuzhii0718.eu.org>
 *
 * QCA board boot command
 *
 * Calls board_boot_default() to start the OS.
 */

#include <command.h>
#include <linux/types.h>

int board_boot_default(bool do_boot);

static int do_qcaboardboot(struct cmd_tbl *cmdtp, int flag, int argc,
			   char *const argv[])
{
	int ret = CMD_RET_SUCCESS;

	ret = board_boot_default(true);
	if (ret)
		ret = CMD_RET_FAILURE;

	if (IS_ENABLED(CONFIG_QCA_WEB_FAILSAFE_AFTER_BOOT_FAILURE))
		run_command("httpd", 0);

	return ret;
}

U_BOOT_CMD(qcaboardboot, 1, 0, do_qcaboardboot,
	   "Boot QCA firmware from flash", ""
);
