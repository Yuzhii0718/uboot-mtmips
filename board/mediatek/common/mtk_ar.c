// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 MediaTek Inc. All Rights Reserved.
 *
 */

#include <errno.h>
#include <image.h>
#include <linux/arm-smccc.h>
#include "mtk_ar.h"

#define FIT_FW_AR_VER_PROP		"fw_ar_ver"

#define MTK_SIP_GET_AR_VER		0xC2000590
#define MTK_SIP_UPDATE_AR_VER		0xC2000591
#define MTK_SIP_LOCK_AR_VER		0xC2000592

enum AR_VER_ID {
	BL_AR_VER_ID = 0,
	FW_AR_VER_ID,
};

static int fit_conf_get_fw_ar_ver(const void *fit, int conf_noffset,
				  uint32_t *ar_ver_p)
{
	const uint32_t *img_ar_ver_p;
	int len = 0;

	img_ar_ver_p = fdt_getprop(fit, conf_noffset, FIT_FW_AR_VER_PROP, &len);
	if (!img_ar_ver_p || len != sizeof(*img_ar_ver_p))
		return -EINVAL;

	*ar_ver_p = fdt32_to_cpu(*img_ar_ver_p);

	return 0;
}

static int sip_get_ar_ver(uint32_t id, uint32_t *ar_ver)
{
	struct arm_smccc_res res = { 0 };

	arm_smccc_smc(MTK_SIP_GET_AR_VER, id, 0, 0, 0, 0, 0, 0, &res);

	if (res.a0)
		return res.a0;

	*ar_ver = res.a1;

	return 0;
}

static int sip_update_ar_ver(uint32_t id, uint32_t ar_ver)
{
	struct arm_smccc_res res = { 0 };

	arm_smccc_smc(MTK_SIP_UPDATE_AR_VER, id, ar_ver, 0, 0, 0, 0, 0, &res);

	return res.a0;
}

static int sip_lock_ar_ver(void)
{
	struct arm_smccc_res res = { 0 };

	arm_smccc_smc(MTK_SIP_LOCK_AR_VER, 0, 0, 0, 0, 0, 0, 0, &res);

	return res.a0;
}

int fit_config_ar_ver_verify(const void *fit, int conf_noffset,
			     uint32_t *ar_ver_p)
{
	uint32_t dev_ar_ver = 0;
	uint32_t img_ar_ver = 0;
	int ret;

	if (!fit || conf_noffset < 0)
		return -EINVAL;

	if (ar_ver_p)
		*ar_ver_p = 0;

	ret = fit_conf_get_fw_ar_ver(fit, conf_noffset, &img_ar_ver);
	if (ret) {
		printf("fw_ar_ver:unavailable\n");
		return ret;
	} else {
		printf("fw_ar_ver:%u", img_ar_ver);
	}

	ret = sip_get_ar_ver(FW_AR_VER_ID, &dev_ar_ver);
	if (ret) {
		if (ret == -ENODEV) {
			/* not support separate FW version, get BL version */
			ret = sip_get_ar_ver(BL_AR_VER_ID, &dev_ar_ver);
			if (ret) {
				printf(",unavailable\n");
				return ret;
			}
		} else {
			printf(",unavailable\n");
			return ret;
		}
	}

	if (img_ar_ver < dev_ar_ver) {
		printf("<%u-\n", dev_ar_ver);
		return -EINVAL;
	} else if (img_ar_ver == dev_ar_ver) {
		printf("=%u+ ", dev_ar_ver);
	} else {
		printf(">%u+ ", dev_ar_ver);
	}

	if (ar_ver_p)
		*ar_ver_p = img_ar_ver;

	return 0;
}

int mtk_ar_update_fw_ar_ver(uint32_t ar_ver)
{
	int ret;

	ret = sip_update_ar_ver(FW_AR_VER_ID, ar_ver);
	if (ret == -ENODEV) {
		/* not support separate FW version, update to BL version */
		ret = sip_update_ar_ver(BL_AR_VER_ID, ar_ver);
	}

	sip_lock_ar_ver();
	return ret;
}

int mtk_ar_set_fdt_fw_ar_ver(void *fdt, int noffset, uint32_t ar_ver)
{
	char buf[4] = "";
	int len;

	if (!fdt || noffset < 0)
		return -EINVAL;

	len = snprintf(buf, sizeof(buf), "%u", ar_ver);
	if (len < 0 || len >= sizeof(buf))
		return -EINVAL;

	return fdt_setprop(fdt, noffset, FIT_FW_AR_VER_PROP, buf, len + 1);
}
