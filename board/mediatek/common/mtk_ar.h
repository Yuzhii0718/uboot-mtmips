/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 MediaTek Inc. All Rights Reserved.
 *
 */
#ifndef _MTK_AR_H_
#define _MTK_AR_H_

int fit_config_ar_ver_verify(const void *fit, int conf_noffset,
			     uint32_t *ar_ver_p);

int mtk_ar_update_fw_ar_ver(uint32_t ar_ver);

int mtk_ar_set_fdt_fw_ar_ver(void *fdt, int noffset, uint32_t ar_ver);

#endif /* _MTK_AR_H_ */
