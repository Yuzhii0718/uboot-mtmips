// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 admin@yuzhii0718.eu.org
 *
 * QCA955X PLL register format (different from QCA956X):
 *   CPU_PLL_CONFIG(0x00): [0:5]NFRAC [6:11]NINT [12:16]REFDIV [19:20]OUTDIV [30]PLLPWD
 *   DDR_PLL_CONFIG(0x04): [0:9]NFRAC [10:15]NINT [16:20]REFDIV [23:25]OUTDIV [30]PLLPWD
 */

#include <clock_legacy.h>
#include <log.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/addrspace.h>
#include <asm/types.h>
#include <mach/ar71xx_regs.h>
#include <mach/ath79.h>
#include <wait_bit.h>

/* QCA955X PLL register bit definitions */
#define CPU_PLL_CONFIG_NFRAC_SHIFT	0
#define CPU_PLL_CONFIG_NFRAC_MASK	0x3f
#define CPU_PLL_CONFIG_NFRAC_SET(x) \
	(((x) << CPU_PLL_CONFIG_NFRAC_SHIFT) & (CPU_PLL_CONFIG_NFRAC_MASK << CPU_PLL_CONFIG_NFRAC_SHIFT))
#define CPU_PLL_CONFIG_NINT_SHIFT	6
#define CPU_PLL_CONFIG_NINT_MASK	0x3f
#define CPU_PLL_CONFIG_NINT_SET(x) \
	(((x) << CPU_PLL_CONFIG_NINT_SHIFT) & (CPU_PLL_CONFIG_NINT_MASK << CPU_PLL_CONFIG_NINT_SHIFT))
#define CPU_PLL_CONFIG_REFDIV_SHIFT	12
#define CPU_PLL_CONFIG_REFDIV_MASK	0x1f
#define CPU_PLL_CONFIG_REFDIV_SET(x) \
	(((x) << CPU_PLL_CONFIG_REFDIV_SHIFT) & (CPU_PLL_CONFIG_REFDIV_MASK << CPU_PLL_CONFIG_REFDIV_SHIFT))
#define CPU_PLL_CONFIG_OUTDIV_SHIFT	19
#define CPU_PLL_CONFIG_OUTDIV_MASK	0x3
#define CPU_PLL_CONFIG_OUTDIV_SET(x) \
	(((x) << CPU_PLL_CONFIG_OUTDIV_SHIFT) & (CPU_PLL_CONFIG_OUTDIV_MASK << CPU_PLL_CONFIG_OUTDIV_SHIFT))
#define CPU_PLL_CONFIG_PLLPWD_SHIFT	30
#define CPU_PLL_CONFIG_PLLPWD_MASK	0x1
#define CPU_PLL_CONFIG_PLLPWD_SET(x) \
	(((x) << CPU_PLL_CONFIG_PLLPWD_SHIFT) & (CPU_PLL_CONFIG_PLLPWD_MASK << CPU_PLL_CONFIG_PLLPWD_SHIFT))

#define DDR_PLL_CONFIG_NFRAC_SHIFT	0
#define DDR_PLL_CONFIG_NFRAC_MASK	0x3ff
#define DDR_PLL_CONFIG_NFRAC_SET(x) \
	(((x) << DDR_PLL_CONFIG_NFRAC_SHIFT) & (DDR_PLL_CONFIG_NFRAC_MASK << DDR_PLL_CONFIG_NFRAC_SHIFT))
#define DDR_PLL_CONFIG_NINT_SHIFT	10
#define DDR_PLL_CONFIG_NINT_MASK	0x3f
#define DDR_PLL_CONFIG_NINT_SET(x) \
	(((x) << DDR_PLL_CONFIG_NINT_SHIFT) & (DDR_PLL_CONFIG_NINT_MASK << DDR_PLL_CONFIG_NINT_SHIFT))
#define DDR_PLL_CONFIG_REFDIV_SHIFT	16
#define DDR_PLL_CONFIG_REFDIV_MASK	0x1f
#define DDR_PLL_CONFIG_REFDIV_SET(x) \
	(((x) << DDR_PLL_CONFIG_REFDIV_SHIFT) & (DDR_PLL_CONFIG_REFDIV_MASK << DDR_PLL_CONFIG_REFDIV_SHIFT))
#define DDR_PLL_CONFIG_OUTDIV_SHIFT	23
#define DDR_PLL_CONFIG_OUTDIV_MASK	0x7
#define DDR_PLL_CONFIG_OUTDIV_SET(x) \
	(((x) << DDR_PLL_CONFIG_OUTDIV_SHIFT) & (DDR_PLL_CONFIG_OUTDIV_MASK << DDR_PLL_CONFIG_OUTDIV_SHIFT))
#define DDR_PLL_CONFIG_PLLPWD_SHIFT	30
#define DDR_PLL_CONFIG_PLLPWD_MASK	0x1
#define DDR_PLL_CONFIG_PLLPWD_SET(x) \
	(((x) << DDR_PLL_CONFIG_PLLPWD_SHIFT) & (DDR_PLL_CONFIG_PLLPWD_MASK << DDR_PLL_CONFIG_PLLPWD_SHIFT))

/* SRIF DPLL2 register layout (QCA955X uses same SRIF base as QCA956X) */
#define QCA955X_SRIF_BASE		(AR71XX_APB_BASE + 0x00116000)
#define QCA955X_SRIF_SIZE		0x1000

#define QCA955X_SRIF_BB_DPLL2_REG	0x180
#define QCA955X_SRIF_PCIE_DPLL2_REG	0xc80
#define QCA955X_SRIF_DDR_DPLL2_REG	0xec0
#define QCA955X_SRIF_CPU_DPLL2_REG	0xf00
#define QCA955X_SRIF_PMU1_REG		0xcc0
#define QCA955X_SRIF_PMU2_REG		0xcc4

#define DPLL2_KI_SHIFT			29
#define DPLL2_KI_MASK			0x3
#define DPLL2_KI_SET(x) \
	(((x) << DPLL2_KI_SHIFT) & (DPLL2_KI_MASK << DPLL2_KI_SHIFT))
#define DPLL2_KD_SHIFT			25
#define DPLL2_KD_MASK			0xf
#define DPLL2_KD_SET(x) \
	(((x) << DPLL2_KD_SHIFT) & (DPLL2_KD_MASK << DPLL2_KD_SHIFT))
#define DPLL2_PLL_PWD_SHIFT		22
#define DPLL2_PLL_PWD_MASK		0x1
#define DPLL2_PLL_PWD_SET(x) \
	(((x) << DPLL2_PLL_PWD_SHIFT) & (DPLL2_PLL_PWD_MASK << DPLL2_PLL_PWD_SHIFT))
#define DPLL2_OUTDIV_SHIFT		19
#define DPLL2_OUTDIV_MASK		0x7
#define DPLL2_OUTDIV_SET(x) \
	(((x) << DPLL2_OUTDIV_SHIFT) & (DPLL2_OUTDIV_MASK << DPLL2_OUTDIV_SHIFT))
#define DPLL2_PHASE_SHIFT_SHIFT		12
#define DPLL2_PHASE_SHIFT_MASK		0x7f
#define DPLL2_PHASE_SHIFT_SET(x) \
	(((x) << DPLL2_PHASE_SHIFT_SHIFT) & (DPLL2_PHASE_SHIFT_MASK << DPLL2_PHASE_SHIFT_SHIFT))

/* Clock control register bits */
#define CLK_CTRL_AHB_DIV_SHIFT		15
#define CLK_CTRL_AHB_DIV_MASK		0x1f
#define CLK_CTRL_AHB_DIV_SET(x) \
	(((x) << CLK_CTRL_AHB_DIV_SHIFT) & (CLK_CTRL_AHB_DIV_MASK << CLK_CTRL_AHB_DIV_SHIFT))
#define CLK_CTRL_DDR_POST_DIV_SHIFT	10
#define CLK_CTRL_DDR_POST_DIV_MASK	0x1f
#define CLK_CTRL_DDR_POST_DIV_SET(x) \
	(((x) << CLK_CTRL_DDR_POST_DIV_SHIFT) & (CLK_CTRL_DDR_POST_DIV_MASK << CLK_CTRL_DDR_POST_DIV_SHIFT))
#define CLK_CTRL_CPU_POST_DIV_SHIFT	5
#define CLK_CTRL_CPU_POST_DIV_MASK	0x1f
#define CLK_CTRL_CPU_POST_DIV_SET(x) \
	(((x) << CLK_CTRL_CPU_POST_DIV_SHIFT) & (CLK_CTRL_CPU_POST_DIV_MASK << CLK_CTRL_CPU_POST_DIV_SHIFT))

/* PLL config values for 720MHz CPU, 600MHz DDR, 200MHz AHB (25MHz xtal) */
#define CPU_NINT_VAL	CPU_PLL_CONFIG_NINT_SET(0x1c)	/* 28 */
#define CPU_NFRAC_VAL	CPU_PLL_CONFIG_NFRAC_SET(0x33)	/* 51 */
#define CPU_REFDIV_VAL	CPU_PLL_CONFIG_REFDIV_SET(1)
#define CPU_OUTDIV_VAL	CPU_PLL_CONFIG_OUTDIV_SET(0)

#define DDR_NINT_VAL	DDR_PLL_CONFIG_NINT_SET(0x18)	/* 24 */
#define DDR_NFRAC_VAL	DDR_PLL_CONFIG_NFRAC_SET(0)
#define DDR_REFDIV_VAL	DDR_PLL_CONFIG_REFDIV_SET(1)
#define DDR_OUTDIV_VAL	DDR_PLL_CONFIG_OUTDIV_SET(0)

DECLARE_GLOBAL_DATA_PTR;

static u32 qca955x_get_xtal(void)
{
	u32 val;

	val = ath79_get_bootstrap();
	if (val & QCA955X_BOOTSTRAP_REF_CLK_40)
		return 40000000;
	else
		return 25000000;
}

int get_serial_clock(void)
{
	return qca955x_get_xtal();
}

void qca955x_pll_init(void)
{
	void __iomem *srif_regs = map_physmem(QCA955X_SRIF_BASE,
					      QCA955X_SRIF_SIZE, MAP_NOCACHE);
	void __iomem *pll_regs = map_physmem(AR71XX_PLL_BASE,
					     AR71XX_PLL_SIZE, MAP_NOCACHE);

	/* Setup BB DPLL2 */
	writel(DPLL2_KI_SET(4) | DPLL2_KD_SET(0x6) |
	       DPLL2_PLL_PWD_SET(1) | DPLL2_PHASE_SHIFT_SET(0x1e),
	       srif_regs + QCA955X_SRIF_BB_DPLL2_REG);

	/* Setup PCIE DPLL2 */
	writel(DPLL2_KI_SET(4) | DPLL2_KD_SET(0x6) |
	       DPLL2_PLL_PWD_SET(1) | DPLL2_PHASE_SHIFT_SET(0x1e),
	       srif_regs + QCA955X_SRIF_PCIE_DPLL2_REG);

	/* Setup DDR DPLL2 */
	writel(DPLL2_KI_SET(4) | DPLL2_KD_SET(0x6) |
	       DPLL2_PLL_PWD_SET(1) | DPLL2_PHASE_SHIFT_SET(0x1e),
	       srif_regs + QCA955X_SRIF_DDR_DPLL2_REG);

	/* Setup CPU DPLL2 */
	writel(DPLL2_KI_SET(4) | DPLL2_KD_SET(0x6) |
	       DPLL2_PLL_PWD_SET(1) | DPLL2_PHASE_SHIFT_SET(0x1e),
	       srif_regs + QCA955X_SRIF_CPU_DPLL2_REG);

	/* Set PLL bypass */
	writel(QCA955X_PLL_CLK_CTRL_CPU_PLL_BYPASS |
	       QCA955X_PLL_CLK_CTRL_DDR_PLL_BYPASS |
	       QCA955X_PLL_CLK_CTRL_AHB_PLL_BYPASS |
	       CLK_CTRL_AHB_DIV_SET(2) |
	       CLK_CTRL_DDR_POST_DIV_SET(0) |
	       CLK_CTRL_CPU_POST_DIV_SET(0) |
	       QCA955X_PLL_CLK_CTRL_AHBCLK_FROM_DDRPLL |
	       QCA955X_PLL_CLK_CTRL_DDRCLK_FROM_DDRPLL |
	       QCA955X_PLL_CLK_CTRL_CPUCLK_FROM_CPUPLL,
	       pll_regs + QCA955X_PLL_CLK_CTRL_REG);

	/* Init CPU PLL (PWD=1) */
	writel(CPU_PLL_CONFIG_PLLPWD_SET(1) |
	       CPU_NINT_VAL | CPU_NFRAC_VAL |
	       CPU_REFDIV_VAL | CPU_OUTDIV_VAL,
	       pll_regs + QCA955X_PLL_CPU_CONFIG_REG);

	/* Init DDR PLL (PWD=1) */
	writel(DDR_PLL_CONFIG_PLLPWD_SET(1) |
	       DDR_NINT_VAL | DDR_NFRAC_VAL |
	       DDR_REFDIV_VAL | DDR_OUTDIV_VAL,
	       pll_regs + QCA955X_PLL_DDR_CONFIG_REG);

	/* Unset PLL PWD */
	writel(CPU_NINT_VAL | CPU_NFRAC_VAL |
	       CPU_REFDIV_VAL | CPU_OUTDIV_VAL,
	       pll_regs + QCA955X_PLL_CPU_CONFIG_REG);

	writel(DDR_NINT_VAL | DDR_NFRAC_VAL |
	       DDR_REFDIV_VAL | DDR_OUTDIV_VAL,
	       pll_regs + QCA955X_PLL_DDR_CONFIG_REG);

	/* Unset PLL bypass */
	writel(CLK_CTRL_AHB_DIV_SET(2) |
	       CLK_CTRL_DDR_POST_DIV_SET(0) |
	       CLK_CTRL_CPU_POST_DIV_SET(0) |
	       QCA955X_PLL_CLK_CTRL_AHBCLK_FROM_DDRPLL |
	       QCA955X_PLL_CLK_CTRL_DDRCLK_FROM_DDRPLL |
	       QCA955X_PLL_CLK_CTRL_CPUCLK_FROM_CPUPLL,
	       pll_regs + QCA955X_PLL_CLK_CTRL_REG);

	/* Wait for PLL lock */
	while (readl(pll_regs + QCA955X_PLL_CPU_CONFIG_REG) & BIT(28))
		/* NOP */;

	while (readl(pll_regs + QCA955X_PLL_DDR_CONFIG_REG) & BIT(28))
		/* NOP */;
}

int get_clocks(void)
{
	void __iomem *regs;
	u32 ref_rate, cpu_rate, ddr_rate, ahb_rate;
	u32 pll, nint, nfrac, ref_div, out_div, clk_ctrl;
	u32 postdiv;

	/*
	 * QCA955x timer init workaround has to be applied right before setting
	 * up the clock. Else, there will be no jiffies
	 */
	regs = map_physmem(AR71XX_RESET_BASE, AR71XX_RESET_SIZE,
			   MAP_NOCACHE);
	pll = readl(regs + AR71XX_RESET_REG_MISC_INT_ENABLE);
	pll |= MISC_INT_MIPS_SI_TIMERINT_MASK;
	writel(pll, regs + AR71XX_RESET_REG_MISC_INT_ENABLE);

	regs = map_physmem(AR71XX_PLL_BASE, AR71XX_PLL_SIZE,
			   MAP_NOCACHE);
	ref_rate = qca955x_get_xtal();

	/* Read CPU PLL config */
	pll = readl(regs + QCA955X_PLL_CPU_CONFIG_REG);
	nfrac = QCA955X_PLL_CPU_CONFIG_NFRAC_MASK & pll;
	nint = (pll >> QCA955X_PLL_CPU_CONFIG_NINT_SHIFT) &
		QCA955X_PLL_CPU_CONFIG_NINT_MASK;
	ref_div = (pll >> QCA955X_PLL_CPU_CONFIG_REFDIV_SHIFT) &
		QCA955X_PLL_CPU_CONFIG_REFDIV_MASK;
	out_div = (pll >> QCA955X_PLL_CPU_CONFIG_OUTDIV_SHIFT) &
		QCA955X_PLL_CPU_CONFIG_OUTDIV_MASK;

	cpu_rate = ref_rate / ref_div;
	cpu_rate = nint * cpu_rate + nfrac * cpu_rate / 64;
	cpu_rate >>= out_div;

	/* Read DDR PLL config */
	pll = readl(regs + QCA955X_PLL_DDR_CONFIG_REG);
	nfrac = QCA955X_PLL_DDR_CONFIG_NFRAC_MASK & pll;
	nint = (pll >> QCA955X_PLL_DDR_CONFIG_NINT_SHIFT) &
		QCA955X_PLL_DDR_CONFIG_NINT_MASK;
	ref_div = (pll >> QCA955X_PLL_DDR_CONFIG_REFDIV_SHIFT) &
		QCA955X_PLL_DDR_CONFIG_REFDIV_MASK;
	out_div = (pll >> QCA955X_PLL_DDR_CONFIG_OUTDIV_SHIFT) &
		QCA955X_PLL_DDR_CONFIG_OUTDIV_MASK;

	ddr_rate = ref_rate / ref_div;
	ddr_rate = nint * ddr_rate + nfrac * ddr_rate / 1024;
	ddr_rate >>= out_div;

	clk_ctrl = readl(regs + QCA955X_PLL_CLK_CTRL_REG);

	/* CPU rate */
	postdiv = (clk_ctrl >> QCA955X_PLL_CLK_CTRL_CPU_POST_DIV_SHIFT) &
		  QCA955X_PLL_CLK_CTRL_CPU_POST_DIV_MASK;

	if (clk_ctrl & QCA955X_PLL_CLK_CTRL_CPU_PLL_BYPASS)
		cpu_rate = ref_rate;
	else if (clk_ctrl & QCA955X_PLL_CLK_CTRL_CPUCLK_FROM_CPUPLL)
		cpu_rate = cpu_rate / (postdiv + 1);
	else
		cpu_rate = ddr_rate / (postdiv + 1); /* CPU from DDR PLL */

	/* DDR rate */
	postdiv = (clk_ctrl >> QCA955X_PLL_CLK_CTRL_DDR_POST_DIV_SHIFT) &
		  QCA955X_PLL_CLK_CTRL_DDR_POST_DIV_MASK;

	if (clk_ctrl & QCA955X_PLL_CLK_CTRL_DDR_PLL_BYPASS)
		ddr_rate = ref_rate;
	else if (clk_ctrl & QCA955X_PLL_CLK_CTRL_DDRCLK_FROM_DDRPLL)
		ddr_rate = ddr_rate / (postdiv + 1);
	else
		ddr_rate = cpu_rate / (postdiv + 1); /* DDR from CPU PLL */

	/* AHB rate */
	postdiv = (clk_ctrl >> QCA955X_PLL_CLK_CTRL_AHB_POST_DIV_SHIFT) &
		  QCA955X_PLL_CLK_CTRL_AHB_POST_DIV_MASK;

	if (clk_ctrl & QCA955X_PLL_CLK_CTRL_AHB_PLL_BYPASS)
		ahb_rate = ref_rate;
	else if (clk_ctrl & QCA955X_PLL_CLK_CTRL_AHBCLK_FROM_DDRPLL)
		ahb_rate = ddr_rate / (postdiv + 1);
	else
		ahb_rate = cpu_rate / (postdiv + 1);

	gd->cpu_clk = cpu_rate;
	gd->mem_clk = ddr_rate;
	gd->bus_clk = ahb_rate;

	debug("cpu_clk=%u, ddr_clk=%u, bus_clk=%u\n",
	      cpu_rate, ddr_rate, ahb_rate);

	return 0;
}

ulong get_bus_freq(ulong dummy)
{
	if (!gd->bus_clk)
		get_clocks();
	return gd->bus_clk;
}

ulong get_ddr_freq(ulong dummy)
{
	if (!gd->mem_clk)
		get_clocks();
	return gd->mem_clk;
}
