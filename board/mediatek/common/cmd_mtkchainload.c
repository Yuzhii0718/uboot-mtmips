// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 *
 * Chainloading bootloader command
 */

#include <command.h>
#include <vsprintf.h>
#include <linux/types.h>

#include "boot_helper.h"

static int do_mtkchainload(struct cmd_tbl *cmdtp, int flag, int argc,
			   char *const argv[])
{
	int ret = CMD_RET_SUCCESS;
	ulong addr;

	if (argc == 1) {
		ret = board_chainload_default(true);
		if (ret)
			ret = CMD_RET_FAILURE;

		if (IS_ENABLED(CONFIG_MTK_WEB_FAILSAFE_AFTER_BOOT_FAILURE))
			run_command("httpd", 0);

		return ret;
	}

	addr = simple_strtoul(argv[1], NULL, 0);

	ret = boot_from_mem(addr);
	if (ret)
		ret = CMD_RET_FAILURE;

	return ret;
}

U_BOOT_CMD(mtkchainload, 2, 0, do_mtkchainload,
	   "Chainload next stage bootloader",
	   "[addr]\n"
	   "  - When [addr] is not set, chainload default next-stage bootloader\n"
	   "  - When [addr] is set, chainload bootloader at memory [addr]\n"
);
