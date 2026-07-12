// SPDX-License-Identifier: GPL-2.0+
/*
 * GPIO Hardware Verification Tool — QCA953X / ARCH_ATH79
 *
 * Copyright (C) 2026 Yuzhii0718 <admin@yuzhii0718.eu.org>
 *
 * Uses direct register access (no DM_GPIO driver available for this SoC).
 *
 * Usage:
 *   gpiotest list                        - Scan all 18 GPIOs, show states
 *   gpiotest blink <pin> [cnt] [ms]      - Blink GPIO (LED verification)
 *   gpiotest monitor <pin>               - Monitor GPIO input (button test)
 *   gpiotest all-leds [start] [end] [ms] - Cycle through GPIOs one by one
 *   gpiotest out <pin> <0|1>             - Set GPIO output value
 *   gpiotest in <pin>                    - Read GPIO input value
 */

#include <command.h>
#include <linux/delay.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <asm/io.h>
#include <mach/ar71xx_regs.h>
#include <linux/errno.h>

/* =================================================================
 * Direct Register Backend — QCA953X
 *
 * GPIO base: 0x18040000
 * OE register: bit=1 → INPUT, bit=0 → OUTPUT  (inverted from typical)
 * SET/CLEAR registers for atomic output writes
 * ================================================================= */

#define GPIOTEST_GPIO_MAX	QCA953X_GPIO_COUNT	/* 18 */

static void __iomem *gpio_regs;

/* Per-pin tracking: whether currently configured as output */
static u8 gpio_is_output[GPIOTEST_GPIO_MAX];

static int gpiotest_init(void)
{
	if (!gpio_regs)
		gpio_regs = map_physmem(AR71XX_GPIO_BASE,
					AR71XX_GPIO_SIZE, MAP_NOCACHE);
	return gpio_regs ? 0 : -ENOMEM;
}

static int gpiotest_request(uint gpio, void *unused)
{
	if (gpio >= GPIOTEST_GPIO_MAX)
		return -EINVAL;
	return gpiotest_init();
}

static int gpiotest_dir_input(uint gpio, void *unused)
{
	/* OE bit = 1 → input */
	setbits_32(gpio_regs + AR71XX_GPIO_REG_OE, BIT(gpio));
	gpio_is_output[gpio] = 0;
	return 0;
}

static int gpiotest_dir_output(uint gpio, int value, void *unused)
{
	/* OE bit = 0 → output */
	clrbits_32(gpio_regs + AR71XX_GPIO_REG_OE, BIT(gpio));
	gpio_is_output[gpio] = 1;

	/* set initial output value via SET/CLEAR register */
	if (value)
		writel(BIT(gpio), gpio_regs + AR71XX_GPIO_REG_SET);
	else
		writel(BIT(gpio), gpio_regs + AR71XX_GPIO_REG_CLEAR);
	return 0;
}

static int gpiotest_read(uint gpio, void *unused)
{
	return !!(readl(gpio_regs + AR71XX_GPIO_REG_IN) & BIT(gpio));
}

static int gpiotest_write(uint gpio, int value, void *unused)
{
	if (value)
		writel(BIT(gpio), gpio_regs + AR71XX_GPIO_REG_SET);
	else
		writel(BIT(gpio), gpio_regs + AR71XX_GPIO_REG_CLEAR);
	return 0;
}

static int gpiotest_get_raw_value(uint gpio)
{
	u32 oe, in, out;
	int ret;

	ret = gpiotest_init();
	if (ret)
		return ret;

	oe  = readl(gpio_regs + AR71XX_GPIO_REG_OE);
	in  = readl(gpio_regs + AR71XX_GPIO_REG_IN);
	out = readl(gpio_regs + AR71XX_GPIO_REG_OUT);

	if (oe & BIT(gpio))
		return !!(in & BIT(gpio));	/* input pin */
	else
		return !!(out & BIT(gpio));	/* output pin */
}

static const char *gpiotest_dir_str(uint gpio)
{
	u32 oe = readl(gpio_regs + AR71XX_GPIO_REG_OE);

	return (oe & BIT(gpio)) ? "IN" : "OUT";
}

/* =================================================================
 * Command Implementations
 * ================================================================= */

static const char *qca953x_gpio_notes[GPIOTEST_GPIO_MAX] = {
	[0]  = "[UART0_SOUT / GPIO]",
	[1]  = "[UART0_SIN  / GPIO]",
	[9]  = "[SPI_CS0    / GPIO]",
	[10] = "[SPI_CS1    / GPIO]",
	[11] = "[SPI_CS2    / GPIO]",
	[12] = "[SPI_CLK    / GPIO]",
	[13] = "[SPI_MOSI   / GPIO]",
	[14] = "[SPI_MISO   / GPIO]",
};

/* ---------- gpiotest list ---------- */
static int do_gpiotest_list(void)
{
	int i, ret;

	ret = gpiotest_init();
	if (ret) {
		printf("ERROR: Cannot map GPIO registers (err=%d)\n", ret);
		return CMD_RET_FAILURE;
	}

	printf("=== QCA953X GPIO Pins (GPIO#0 ~ GPIO#%d) ===\n",
	       GPIOTEST_GPIO_MAX - 1);
	printf("%-4s %-4s %-5s %-7s %s\n",
	       "PIN", "DIR", "VALUE", "OE-REG", "NOTES");
	printf("---- ---- ----- ------- ---------------------------------------\n");

	for (i = 0; i < GPIOTEST_GPIO_MAX; i++) {
		const char *dir = gpiotest_dir_str(i);
		int val = gpiotest_get_raw_value(i);
		u32 oe = readl(gpio_regs + AR71XX_GPIO_REG_OE);
		const char *notes = qca953x_gpio_notes[i] ?
				    qca953x_gpio_notes[i] : "";

		if (val < 0)
			printf("%-4d %-4s %-5s %-7s %s\n",
			       i, "?", "ERR", "", notes);
		else
			printf("%-4d %-4s %-5d %s(0x%x)  %s\n",
			       i, dir, val,
			       (oe & BIT(i)) ? "IN  " : "OUT ",
			       (unsigned int)(oe & BIT(i)), notes);
	}

	printf("---- ---- ----- ------- ---------------------------------------\n");
	printf("OE bit=1: INPUT,  OE bit=0: OUTPUT\n");
	printf("Total GPIOs: %d\n", GPIOTEST_GPIO_MAX);

	return CMD_RET_SUCCESS;
}

/* ---------- gpiotest blink ---------- */
static int do_gpiotest_blink(uint gpio_num, int count, int interval_ms)
{
	int ret, i;

	if (count <= 0)
		count = 3;
	if (interval_ms <= 0)
		interval_ms = 200;

	ret = gpiotest_request(gpio_num, NULL);
	if (ret) {
		printf("ERROR: Invalid GPIO#%d\n", gpio_num);
		return CMD_RET_FAILURE;
	}

	ret = gpiotest_dir_output(gpio_num, 0, NULL);
	if (ret) {
		printf("ERROR: Cannot set GPIO#%d as output\n", gpio_num);
		return CMD_RET_FAILURE;
	}

	printf("Blinking GPIO#%d: %d times @ %dms interval\n",
	       gpio_num, count, interval_ms);

	for (i = 0; i < count; i++) {
		printf("\r  [%d/%d] ON  ", i + 1, count);
		gpiotest_write(gpio_num, 1, NULL);
		mdelay(interval_ms);

		printf("\r  [%d/%d] OFF ", i + 1, count);
		gpiotest_write(gpio_num, 0, NULL);
		mdelay(interval_ms);
	}

	gpiotest_write(gpio_num, 0, NULL);
	printf("\r  Done. GPIO#%d set to LOW.          \n", gpio_num);

	return CMD_RET_SUCCESS;
}

/* ---------- gpiotest monitor ---------- */
static int do_gpiotest_monitor(uint gpio_num)
{
	ulong start_time;
	int ret;
	int last_val = -1;
	int val;

	ret = gpiotest_request(gpio_num, NULL);
	if (ret) {
		printf("ERROR: Invalid GPIO#%d\n", gpio_num);
		return CMD_RET_FAILURE;
	}

	/* save current direction, then set as input */
	u8 was_output = gpio_is_output[gpio_num];

	ret = gpiotest_dir_input(gpio_num, NULL);
	if (ret) {
		printf("ERROR: Cannot set GPIO#%d as input\n", gpio_num);
		return CMD_RET_FAILURE;
	}

	printf("Monitoring GPIO#%d (press any key to stop)...\n", gpio_num);
	printf("Initial state: ");
	val = gpiotest_read(gpio_num, NULL);
	printf("%d (%s)\n", val, val ? "HIGH" : "LOW");
	last_val = val;
	printf("--- Waiting for state changes ---\n");

	start_time = get_timer(0);

	while (1) {
		if (tstc()) {
			getchar();
			printf("\n--- Monitoring stopped by user ---\n");
			break;
		}

		val = gpiotest_read(gpio_num, NULL);
		if (val < 0) {
			printf("ERROR reading GPIO#%d\n", gpio_num);
			break;
		}

		if (val != last_val) {
			ulong elapsed = get_timer(start_time) / 1000;

			printf("[%3lu.%03lus] GPIO#%d: %d -> %d (%s -> %s)\n",
			       elapsed / 1000, elapsed % 1000,
			       gpio_num, last_val, val,
			       last_val ? "HIGH" : "LOW",
			       val ? "HIGH" : "LOW");
			last_val = val;
		}

		mdelay(10);
	}

	printf("GPIO#%d final state: %d\n", gpio_num, last_val);

	/* restore original direction */
	if (was_output) {
		clrbits_32(gpio_regs + AR71XX_GPIO_REG_OE, BIT(gpio_num));
		gpio_is_output[gpio_num] = 1;
	} else {
		setbits_32(gpio_regs + AR71XX_GPIO_REG_OE, BIT(gpio_num));
		gpio_is_output[gpio_num] = 0;
	}

	return CMD_RET_SUCCESS;
}

/* ---------- gpiotest all-leds ---------- */
static int do_gpiotest_all_leds(uint start, uint end, int interval_ms)
{
	uint i;

	if (end >= GPIOTEST_GPIO_MAX)
		end = GPIOTEST_GPIO_MAX - 1;
	if (start > end) {
		uint tmp = start;
		start = end;
		end = tmp;
	}
	if (interval_ms <= 0)
		interval_ms = 500;

	printf("Cycling GPIO#%u ~ GPIO#%u one by one (%dms each)\n",
	       start, end, interval_ms);
	printf("Watch the LEDs to identify which GPIO controls which LED.\n");
	printf("(Press any key to skip current pin)\n\n");

	for (i = start; i <= end; i++) {
		int ret;

		ret = gpiotest_request(i, NULL);
		if (ret)
			continue;

		ret = gpiotest_dir_output(i, 0, NULL);
		if (ret)
			continue;

		printf("\rGPIO#%u [ON ]", i);
		gpiotest_write(i, 1, NULL);
		mdelay(interval_ms);

		if (tstc()) {
			getchar();
			printf(" (skipped)");
			mdelay(500);
		}

		printf("\rGPIO#%u [OFF]", i);
		gpiotest_write(i, 0, NULL);
		mdelay(200);
	}

	printf("\rDone. All GPIOs restored to LOW.                              \n");

	return CMD_RET_SUCCESS;
}

/* ---------- gpiotest out ---------- */
static int do_gpiotest_out(uint gpio_num, int value)
{
	int ret;

	ret = gpiotest_request(gpio_num, NULL);
	if (ret) {
		printf("ERROR: Invalid GPIO#%d\n", gpio_num);
		return CMD_RET_FAILURE;
	}

	ret = gpiotest_dir_output(gpio_num, value, NULL);
	if (ret) {
		printf("ERROR: Cannot set GPIO#%d as output\n", gpio_num);
		return CMD_RET_FAILURE;
	}

	printf("GPIO#%d -> %d (%s)\n", gpio_num, value, value ? "HIGH" : "LOW");
	return CMD_RET_SUCCESS;
}

/* ---------- gpiotest in ---------- */
static int do_gpiotest_in(uint gpio_num)
{
	int ret, val;

	ret = gpiotest_request(gpio_num, NULL);
	if (ret) {
		printf("ERROR: Invalid GPIO#%d\n", gpio_num);
		return CMD_RET_FAILURE;
	}

	ret = gpiotest_dir_input(gpio_num, NULL);
	if (ret) {
		printf("ERROR: Cannot set GPIO#%d as input\n", gpio_num);
		return CMD_RET_FAILURE;
	}

	val = gpiotest_read(gpio_num, NULL);
	printf("GPIO#%d = %d (%s)\n", gpio_num, val, val ? "HIGH" : "LOW");

	return CMD_RET_SUCCESS;
}

/* =================================================================
 * Command Dispatcher
 * ================================================================= */

static int do_gpiotest(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	const char *subcmd;

	if (argc < 2)
		return CMD_RET_USAGE;

	subcmd = argv[1];

	/* gpiotest list */
	if (!strcmp(subcmd, "list"))
		return do_gpiotest_list();

	/* gpiotest blink <pin> [count] [interval_ms] */
	if (!strcmp(subcmd, "blink")) {
		uint pin;
		int count = 3, interval = 200;

		if (argc < 3)
			return CMD_RET_USAGE;
		pin = dectoul(argv[2], NULL);
		if (argc >= 4)
			count = dectoul(argv[3], NULL);
		if (argc >= 5)
			interval = dectoul(argv[4], NULL);
		return do_gpiotest_blink(pin, count, interval);
	}

	/* gpiotest monitor <pin> */
	if (!strcmp(subcmd, "monitor")) {
		uint pin;

		if (argc < 3)
			return CMD_RET_USAGE;
		pin = dectoul(argv[2], NULL);
		return do_gpiotest_monitor(pin);
	}

	/* gpiotest all-leds [start] [end] [interval_ms] */
	if (!strcmp(subcmd, "all-leds")) {
		uint start = 0, end = GPIOTEST_GPIO_MAX - 1;
		int interval = 500;

		if (argc >= 3)
			start = dectoul(argv[2], NULL);
		if (argc >= 4)
			end = dectoul(argv[3], NULL);
		if (argc >= 5)
			interval = dectoul(argv[4], NULL);
		return do_gpiotest_all_leds(start, end, interval);
	}

	/* gpiotest out <pin> <0|1> */
	if (!strcmp(subcmd, "out")) {
		uint pin;
		int value;

		if (argc < 4)
			return CMD_RET_USAGE;
		pin = dectoul(argv[2], NULL);
		value = dectoul(argv[3], NULL) ? 1 : 0;
		return do_gpiotest_out(pin, value);
	}

	/* gpiotest in <pin> */
	if (!strcmp(subcmd, "in")) {
		uint pin;

		if (argc < 3)
			return CMD_RET_USAGE;
		pin = dectoul(argv[2], NULL);
		return do_gpiotest_in(pin);
	}

	printf("Unknown subcommand: %s\n", subcmd);
	return CMD_RET_USAGE;
}

U_BOOT_CMD(
	gpiotest, 6, 0, do_gpiotest,
	"GPIO hardware verification tool (QCA953X direct register)",
	"list                         - Scan all 18 GPIOs, show states\n"
	"blink <pin> [cnt] [ms]       - Blink GPIO for LED identification\n"
	"monitor <pin>                - Monitor GPIO input for button test\n"
	"all-leds [start] [end] [ms]  - Cycle through GPIOs one by one\n"
	"out <pin> <0|1>              - Set GPIO output value\n"
	"in <pin>                     - Read GPIO input value\n"
	"\n"
	"QCA953X has 18 GPIOs (GPIO#0 ~ GPIO#17)\n"
	"OE bit=1: INPUT, OE bit=0: OUTPUT"
);
