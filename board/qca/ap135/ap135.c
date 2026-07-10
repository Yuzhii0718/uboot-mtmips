// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 admin@yuzhii0718.eu.org
 *
 * AP135 Reference Board initialization.
 */

#include <init.h>
#include <asm/io.h>
#include <asm/addrspace.h>
#include <asm/types.h>
#include <mach/ar71xx_regs.h>
#include <mach/ddr.h>
#include <mach/ath79.h>
#include <debug_uart.h>

#define RST_RESET_RTC_RESET_LSB 27
#define RST_RESET_RTC_RESET_MASK 0x08000000
#define RST_RESET_RTC_RESET_SET(x) \
	(((x) << RST_RESET_RTC_RESET_LSB) & RST_RESET_RTC_RESET_MASK)

#ifdef CONFIG_DEBUG_UART_BOARD_INIT
void board_debug_uart_init(void)
{
	void __iomem *regs;
	u32 val;

	regs = map_physmem(AR71XX_GPIO_BASE, AR71XX_GPIO_SIZE,
			   MAP_NOCACHE);

	/*
	 * QCA955X UART uses GPIO8 (RX) and GPIO9 (TX) by default
	 * Set GPIO8 as input, GPIO9 as output
	 */
	val = readl(regs + AR71XX_GPIO_REG_OE);
	val |= QCA955X_GPIO(8);
	val &= ~QCA955X_GPIO(9);
	writel(val, regs + AR71XX_GPIO_REG_OE);

	/*
	 * Enable GPIO9 as UART0_SOUT
	 */
	val = readl(regs + QCA955X_GPIO_REG_OUT_FUNC0);
	val &= ~(0xff << 0);
	val |= (0x23) << 0;
	writel(val, regs + QCA955X_GPIO_REG_OUT_FUNC0);

	/*
	 * Enable GPIO8 as UART0_SIN
	 */
	val = readl(regs + QCA955X_GPIO_REG_IN_ENABLE0);
	val &= ~(0xff << 0);
	val |= (0x4) << 0;
	writel(val, regs + QCA955X_GPIO_REG_IN_ENABLE0);

	/*
	 * Enable GPIO9 output
	 */
	val = readl(regs + AR71XX_GPIO_REG_OUT);
	val |= QCA955X_GPIO(9);
	writel(val, regs + AR71XX_GPIO_REG_OUT);
}
#endif

int board_early_init_f(void)
{
	u32 reg;
	void __iomem *rst_regs = map_physmem(AR71XX_RESET_BASE,
					     AR71XX_RESET_SIZE, MAP_NOCACHE);

#if !CONFIG_IS_ENABLED(SKIP_LOWLEVEL_INIT)
	/* CPU:720, DDR:600, AHB:200 */
	qca955x_pll_init();
	qca955x_ddr_init();
#endif

	/* Take WMAC out of reset */
	reg = readl(rst_regs + QCA955X_RESET_REG_RESET_MODULE);
	reg &= (~RST_RESET_RTC_RESET_SET(1));
	writel(reg, rst_regs + QCA955X_RESET_REG_RESET_MODULE);

	ath79_eth_reset();
	return 0;
}
