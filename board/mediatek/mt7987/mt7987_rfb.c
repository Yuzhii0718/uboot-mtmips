// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 MediaTek Inc.
 * Author: Sam Shih <sam.shih@mediatek.com>
 */

#include <errno.h>
#include <malloc.h>
#include <init.h>
#include <asm/io.h>
#include <asm/global_data.h>
#include <linux/sizes.h>
#include <linux/types.h>
#include <linux/log2.h>
#include "../common/unxz.h"

DECLARE_GLOBAL_DATA_PTR;

#define	MT7987_BOOT_SD		0
#define	MT7987_BOOT_NOR		1
#define	MT7987_BOOT_SPIM_NAND	2
#define	MT7987_BOOT_EMMC	3

const char *mtk_board_rootdisk(void)
{
	switch ((readl(0x1001f6f0) & 0xc0) >> 6) {
	case MT7987_BOOT_SD:
		return "sd";

	case MT7987_BOOT_NOR:
		return "nor";

	case MT7987_BOOT_SPIM_NAND:
		return "spim-nand";

	case MT7987_BOOT_EMMC:
		return "emmc";

	default:
		return "";
	}
}

ulong board_get_load_addr(void)
{
	ulong half_size = (get_effective_memsize() / 2) & ~(SZ_16M - 1);

	return gd->ram_base + half_size;
}

#define MT7987_2P5GE_PMB_FW_SIZE		0x18000
#define MT7987_2P5GE_DSPBITTB_SIZE		0x7000

extern const u8 i2p5ge_phy_pmb[];
extern const u32 i2p5ge_phy_pmb_size;
extern const u8 i2p5ge_phy_dspbit[];
extern const u32 i2p5ge_phy_dspbit_size;

int mt7987_i2p5ge_get_fw(const void **fw, size_t *fwsize, const void **dspfw,
			 size_t *dspfwsize)
{
#ifdef CONFIG_XZ
	void *pmb_data = NULL, *dsp_data;
	int ret;

	if (memcmp(i2p5ge_phy_pmb, xz_magic, sizeof(xz_magic))) {
		*fw = i2p5ge_phy_pmb;
		*fwsize = i2p5ge_phy_pmb_size;
	} else {
		pmb_data = malloc(MT7987_2P5GE_PMB_FW_SIZE);
		if (!pmb_data)
			return -ENOMEM;

		ret = unxz(i2p5ge_phy_pmb, i2p5ge_phy_pmb_size, fwsize,
			   pmb_data, MT7987_2P5GE_PMB_FW_SIZE);
		if (ret) {
			free(pmb_data);
			return -1;
		}

		*fw = pmb_data;
	}

	if (memcmp(i2p5ge_phy_dspbit, xz_magic, sizeof(xz_magic))) {
		*dspfw = i2p5ge_phy_dspbit;
		*dspfwsize = i2p5ge_phy_dspbit_size;
	} else {
		dsp_data = malloc(MT7987_2P5GE_DSPBITTB_SIZE);
		if (!dsp_data) {
			if (pmb_data)
				free(pmb_data);
			return -ENOMEM;
		}

		ret = unxz(i2p5ge_phy_dspbit, i2p5ge_phy_dspbit_size, dspfwsize,
			   dsp_data, MT7987_2P5GE_DSPBITTB_SIZE);
		if (ret) {
			free(dsp_data);

			if (pmb_data)
				free(pmb_data);

			return -1;
		}

		*dspfw = dsp_data;
	}
#else
	*fw = i2p5ge_phy_pmb;
	*fwsize = i2p5ge_phy_pmb_size;
	*dspfw = i2p5ge_phy_dspbit;
	*dspfwsize = i2p5ge_phy_dspbit_size;
#endif

	return 0;
}
