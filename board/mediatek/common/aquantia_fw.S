/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 */

	.section	.rodata.aquantia_fw, "a"
	.align	2
	.globl	aquantia_fw
	.type	aquantia_fw, %object
aquantia_fw:
#ifdef CONFIG_XZ
	.incbin	"aquantia_fw.xz"
#else
	.incbin	"aquantia_fw.cld"
#endif
	.equ	_aquantia_fw_size, . - aquantia_fw
	.size	aquantia_fw, _aquantia_fw_size

	.section	.rodata.aquantia_fw_size, "a"
	.align	2
	.globl	aquantia_fw_size
	.type	aquantia_fw_size, %object
aquantia_fw_size:
	.word	_aquantia_fw_size
	.size	aquantia_fw_size, . - aquantia_fw_size
