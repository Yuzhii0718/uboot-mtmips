// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 MediaTek Incorporation. All Rights Reserved.
 *
 */

#include <linux/arm-smccc.h>

#define MTK_SIP_FW_DEC_CLEANUP			0xC2000584

int fw_dec_cleanup(void)
{
	struct arm_smccc_res res = { 0 };

	arm_smccc_smc(MTK_SIP_FW_DEC_CLEANUP, 0, 0, 0, 0, 0, 0, 0, &res);

	return res.a0;
}
