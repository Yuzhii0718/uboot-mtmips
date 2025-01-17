/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 */

	.section	.rodata.i2p5ge_phy_pmb, "a"
	.align	2
	.globl	i2p5ge_phy_pmb
	.type	i2p5ge_phy_pmb, %object
i2p5ge_phy_pmb:
#ifdef CONFIG_XZ
	.incbin	"mt7987-i2p5ge-phy-pmb.xz"
#else
	.incbin	"mt7987-i2p5ge-phy-pmb.bin"
#endif
	.equ	_i2p5ge_phy_pmb_size, . - i2p5ge_phy_pmb
	.size	i2p5ge_phy_pmb, _i2p5ge_phy_pmb_size

	.section	.rodata.i2p5ge_phy_pmb_size, "a"
	.align	2
	.globl	i2p5ge_phy_pmb_size
	.type	i2p5ge_phy_pmb_size, %object
i2p5ge_phy_pmb_size:
	.word	_i2p5ge_phy_pmb_size
	.size	i2p5ge_phy_pmb_size, . - i2p5ge_phy_pmb_size

	.section	.rodata.i2p5ge_phy_dspbit, "a"
	.align	2
	.globl	i2p5ge_phy_dspbit
	.type	i2p5ge_phy_dspbit, %object
i2p5ge_phy_dspbit:
#ifdef CONFIG_XZ
	.incbin	"mt7987-i2p5ge-phy-DSPBitTb.xz"
#else
	.incbin	"mt7987-i2p5ge-phy-DSPBitTb.bin"
#endif
	.equ	_i2p5ge_phy_dspbit_size, . - i2p5ge_phy_dspbit
	.size	i2p5ge_phy_dspbit, _i2p5ge_phy_dspbit_size

	.section	.rodata.i2p5ge_phy_dspbit_size, "a"
	.align	2
	.globl	i2p5ge_phy_dspbit_size
	.type	i2p5ge_phy_dspbit_size, %object
i2p5ge_phy_dspbit_size:
	.word	_i2p5ge_phy_dspbit_size
	.size	i2p5ge_phy_dspbit_size, . - i2p5ge_phy_dspbit_size
