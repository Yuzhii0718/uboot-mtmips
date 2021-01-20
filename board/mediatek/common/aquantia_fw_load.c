/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 *
 * Aquantia PHY embedded FW loader
 */

#include <malloc.h>
#include <stdio.h>
#include <linux/errno.h>
#include <linux/types.h>
#include "unxz.h"

#define AQR_FW_MAX_SIZE			0x64000

struct phy_device;

extern const u8 aquantia_fw[];
extern const u32 aquantia_fw_size;

static int aquantia_read_fw_direct(u8 **fw_addr, size_t *fw_length)
{
	void *data;

	data = malloc(aquantia_fw_size);
	if (!data)
		return -ENOMEM;

	memcpy(data, aquantia_fw, aquantia_fw_size);

	*fw_addr = data;
	*fw_length = aquantia_fw_size;

	return 0;
}

int aquantia_read_fw(struct phy_device *phydev, u8 **fw_addr, size_t *fw_length)
{
	void *data;

#ifdef CONFIG_XZ
	int ret;

	if (memcmp(aquantia_fw, xz_magic, sizeof(xz_magic)))
		return aquantia_read_fw_direct(fw_addr, fw_length);

	data = malloc(AQR_FW_MAX_SIZE);
	if (!data)
		return -ENOMEM;

	ret = unxz(aquantia_fw, aquantia_fw_size, fw_length, data,
		   AQR_FW_MAX_SIZE);
	if (ret) {
		free(data);
		return -1;
	}

	*fw_addr = data;

	return 0;
#else
	return aquantia_read_fw_direct(fw_addr, fw_length);
#endif
}
