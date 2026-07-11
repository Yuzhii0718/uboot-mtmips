/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 MediaTek Inc.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 */

#ifndef __CONFIG_MT7628_H
#define __CONFIG_MT7628_H

#define CFG_SYS_SDRAM_BASE		0x80000000

#if (defined(CONFIG_CONS_INDEX) && CONFIG_CONS_INDEX == 3) || \
    defined(CONFIG_MT7628_BOARD_GARDENA)
#define CFG_SYS_INIT_SP_OFFSET		0x400000
#else
#define CFG_SYS_INIT_SP_OFFSET		0x80000
#endif

/* Serial SPL */
#if defined(CONFIG_XPL_BUILD) && defined(CONFIG_SPL_SERIAL)
#define CFG_SYS_NS16550_CLK		40000000
#if defined(CONFIG_CONS_INDEX) && CONFIG_CONS_INDEX == 3
#define CFG_SYS_NS16550_COM3		0xb0000e00
#else
#define CFG_SYS_NS16550_COM1		0xb0000c00
#endif
#endif

/* Serial common */
#define CFG_SYS_BAUDRATE_TABLE		{ 9600, 19200, 38400, 57600, 115200, \
					  230400, 460800, 921600 }

/* Dummy value */
#define CFG_SYS_UBOOT_BASE		0

#endif /* __CONFIG_MT7628_H */
