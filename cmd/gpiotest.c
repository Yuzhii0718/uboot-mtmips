// SPDX-License-Identifier: GPL-2.0+
/*
 * GPIO Hardware Verification Tool for MT7628 / MTMIPS
 *
 * Provides systematic GPIO testing commands for hardware bringup
 * without requiring OpenWrt/Linux.
 *
 * Usage:
 *   gpiotest list [bank]          - Scan all GPIOs, show pin states
 *   gpiotest blink <pin> [cnt] [ms] - Blink GPIO (LED verification)
 *   gpiotest monitor <pin>        - Monitor GPIO input (button identification)
 *   gpiotest all-leds [start] [end] [ms] - Blink all pins in range one by one
 */

#include <command.h>
#include <dm.h>
#include <dm/uclass.h>
#include <asm/gpio.h>
#include <linux/delay.h>
#include <time.h>
#include <stdlib.h>

/*
 * MT7628 GPIO layout (via mt7621_gpio.c):
 *   Bank 0 (gpio0): GPIO#0  ~ GPIO#31
 *   Bank 1 (gpio1): GPIO#32 ~ GPIO#63
 *   Bank 2 (gpio2): GPIO#64 ~ GPIO#95
 */
#define MT7628_GPIO_MAX  96
#define MT7628_BANK0_MAX 32
#define MT7628_BANK1_MAX 64
#define MT7628_BANK2_MAX 96
#define MT7628_BANK_COUNT 3

/**
 * gpiotest_request() - Look up + request a GPIO by global number
 *
 * Tries uclass bank traversal first, then dm_gpio_lookup_name() fallback.
 * On success, the GPIO is requested with label "gpiotest".
 * Caller must dm_gpio_free() when done.
 *
 * @gpio_num: global GPIO number (0-95 for MT7628)
 * @desc:     gpio_desc to fill (requested, ready to use)
 * Return: 0 on success, negative errno on failure
 */
static int gpiotest_request(uint gpio_num, struct gpio_desc *desc)
{
	struct udevice *dev;
	int bank_offset, pin_in_bank;
	char name[32];
	int ret;

	if (gpio_num >= MT7628_GPIO_MAX)
		return -EINVAL;

	/* Determine bank */
	if (gpio_num < MT7628_BANK0_MAX) {
		bank_offset = 0;
		pin_in_bank = gpio_num;
	} else if (gpio_num < MT7628_BANK1_MAX) {
		bank_offset = 1;
		pin_in_bank = gpio_num - MT7628_BANK0_MAX;
	} else {
		bank_offset = 2;
		pin_in_bank = gpio_num - MT7628_BANK1_MAX;
	}

	/* Walk uclass GPIO devices to match bank reg */
	uclass_first_device(UCLASS_GPIO, &dev);
	while (dev) {
		uint reg = dev_read_u32_default(dev, "reg", 999);

		if (reg == (uint)bank_offset) {
			desc->dev = dev;
			desc->offset = pin_in_bank;
			desc->flags = 0;

			ret = dm_gpio_request(desc, "gpiotest");
			if (ret)
				return ret;
			return 0;
		}
		uclass_next_device(&dev);
	}

	/* Fallback: use dm_gpio_lookup_name */
	snprintf(name, sizeof(name), "%d:%d", bank_offset, pin_in_bank);
	ret = dm_gpio_lookup_name(name, desc);
	if (ret)
		return ret;

	ret = dm_gpio_request(desc, "gpiotest");
	return ret;
}

/* ==================== gpiotest list ==================== */

static int do_gpiotest_list(int bank_filter)
{
	int start_gpio = 0, end_gpio = MT7628_GPIO_MAX - 1;
	struct gpio_desc desc;
	int i, ret;
	int tested = 0, ok = 0, failed = 0;

	if (bank_filter >= 0) {
		start_gpio = bank_filter * 32;
		end_gpio = start_gpio + 31;
		if (end_gpio >= MT7628_GPIO_MAX)
			end_gpio = MT7628_GPIO_MAX - 1;
		printf("=== GPIO Bank %d (GPIO#%d ~ GPIO#%d) ===\n",
		       bank_filter, start_gpio, end_gpio);
	} else {
		printf("=== All GPIO Pins (GPIO#0 ~ GPIO#%d) ===\n",
		       MT7628_GPIO_MAX - 1);
	}
	printf("%-4s %-12s %-8s %s\n", "PIN", "DIRECTION", "VALUE", "NOTES");
	printf("---- ------------ -------- --------------------\n");

	for (i = start_gpio; i <= end_gpio; i++) {
		int val;
		const char *dir_str = "?";
		char note[40] = "";

		/* Request GPIO (mt7621 driver requires request before use) */
		ret = gpiotest_request(i, &desc);
		if (ret)
			continue;

		tested++;

		/* Try reading as input */
		ret = dm_gpio_set_dir_flags(&desc, GPIOD_IS_IN);
		if (!ret) {
			val = dm_gpio_get_value(&desc);
			if (val >= 0) {
				ok++;
				dir_str = "IN";
			} else {
				failed++;
				dir_str = "ERR";
			}
		} else {
			failed++;
			dir_str = "n/a";
			val = -1;
		}

		dm_gpio_free(desc.dev, &desc);

		/* Add human-friendly notes for known pins */
		switch (i) {
		case 38:
			snprintf(note, sizeof(note), "[likely RESET btn]");
			break;
		case 11:
			snprintf(note, sizeof(note), "[likely WPS btn]");
			break;
		case 43:
			snprintf(note, sizeof(note), "[likely Power LED]");
			break;
		case 44:
			snprintf(note, sizeof(note), "[WLAN LED wled_an]");
			break;
		case 37:
		case 39:
		case 40:
		case 41:
		case 42:
			snprintf(note, sizeof(note), "[WLAN LED alt pin]");
			break;
		}

		if (val >= 0)
			printf("%-4d %-12s %-8d %s\n",
			       i, dir_str, val, note);
		else
			printf("%-4d %-12s %-8s %s\n",
			       i, dir_str, "ERR", note);
	}

	printf("---- ------------ -------- --------------------\n");
	printf("Tested: %d, OK: %d", tested, ok);
	if (failed > 0)
		printf(", Failed: %d", failed);
	printf("\n");

	return CMD_RET_SUCCESS;
}

/* ==================== gpiotest blink ==================== */

static int do_gpiotest_blink(uint gpio_num, int count, int interval_ms)
{
	struct gpio_desc desc;
	int ret, i;

	if (count <= 0)
		count = 3;
	if (interval_ms <= 0)
		interval_ms = 200;

	ret = gpiotest_request(gpio_num, &desc);
	if (ret) {
		printf("ERROR: Cannot request GPIO#%d (err=%d)\n", gpio_num, ret);
		return CMD_RET_FAILURE;
	}

	/* Configure as output */
	ret = dm_gpio_set_dir_flags(&desc, GPIOD_IS_OUT);
	if (ret) {
		printf("ERROR: Cannot set GPIO#%d as output (err=%d)\n",
		       gpio_num, ret);
		dm_gpio_free(desc.dev, &desc);
		return CMD_RET_FAILURE;
	}

	printf("Blinking GPIO#%d: %d times @ %dms interval\n",
	       gpio_num, count, interval_ms);

	for (i = 0; i < count; i++) {
		printf("\r  [%d/%d] ON  ", i + 1, count);
		dm_gpio_set_value(&desc, 1);
		mdelay(interval_ms);

		printf("\r  [%d/%d] OFF ", i + 1, count);
		dm_gpio_set_value(&desc, 0);
		mdelay(interval_ms);
	}

	dm_gpio_set_value(&desc, 0);
	printf("\r  Done. GPIO#%d set to LOW.          \n", gpio_num);

	dm_gpio_free(desc.dev, &desc);
	return CMD_RET_SUCCESS;
}

/* ==================== gpiotest monitor ==================== */

static int do_gpiotest_monitor(uint gpio_num)
{
	struct gpio_desc desc;
	ulong start_time;
	int ret;
	int last_val = -1;
	int val;

	ret = gpiotest_request(gpio_num, &desc);
	if (ret) {
		printf("ERROR: Cannot request GPIO#%d (err=%d)\n", gpio_num, ret);
		return CMD_RET_FAILURE;
	}

	/* Configure as input */
	ret = dm_gpio_set_dir_flags(&desc, GPIOD_IS_IN);
	if (ret) {
		printf("ERROR: Cannot set GPIO#%d as input (err=%d)\n",
		       gpio_num, ret);
		dm_gpio_free(desc.dev, &desc);
		return CMD_RET_FAILURE;
	}

	printf("Monitoring GPIO#%d (press any key to stop)...\n", gpio_num);
	printf("Initial state: ");
	val = dm_gpio_get_value(&desc);
	if (val >= 0) {
		printf("%d (%s)\n", val, val ? "HIGH" : "LOW");
		last_val = val;
	} else {
		printf("ERROR\n");
		dm_gpio_free(desc.dev, &desc);
		return CMD_RET_FAILURE;
	}
	printf("--- Waiting for state changes ---\n");

	start_time = get_timer(0);

	while (1) {
		if (tstc()) {
			getchar();
			printf("\n--- Monitoring stopped by user ---\n");
			break;
		}

		val = dm_gpio_get_value(&desc);
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
	dm_gpio_free(desc.dev, &desc);

	return CMD_RET_SUCCESS;
}

/* ==================== gpiotest all-leds ==================== */

static int do_gpiotest_all_leds(uint start, uint end, int interval_ms)
{
	uint i;

	if (end >= MT7628_GPIO_MAX)
		end = MT7628_GPIO_MAX - 1;
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
	printf("(Press Ctrl+C to skip current pin)\n\n");

	for (i = start; i <= end; i++) {
		struct gpio_desc desc;
		int ret;

		ret = gpiotest_request(i, &desc);
		if (ret)
			continue;

		ret = dm_gpio_set_dir_flags(&desc, GPIOD_IS_OUT);
		if (ret) {
			dm_gpio_free(desc.dev, &desc);
			continue;
		}

		printf("\rGPIO#%u [ON ]", i);
		dm_gpio_set_value(&desc, 1);
		mdelay(interval_ms);

		/* Check for keypress to skip */
		if (tstc()) {
			getchar();
			printf(" (skipped)");
			mdelay(500);
		}

		printf("\rGPIO#%u [OFF]", i);
		dm_gpio_set_value(&desc, 0);
		mdelay(200);

		dm_gpio_free(desc.dev, &desc);
	}

	printf("\rDone. All GPIOs restored to LOW.                              \n");

	return CMD_RET_SUCCESS;
}

/* ==================== command dispatcher ==================== */

static int do_gpiotest(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	const char *subcmd;

	if (argc < 2)
		return CMD_RET_USAGE;

	subcmd = argv[1];

	/* gpiotest list [bank] */
	if (!strcmp(subcmd, "list")) {
		int bank = -1;

		if (argc >= 3)
			bank = dectoul(argv[2], NULL);
		if (bank >= MT7628_BANK_COUNT) {
			printf("ERROR: bank must be 0~%d\n",
			       MT7628_BANK_COUNT - 1);
			return CMD_RET_FAILURE;
		}
		return do_gpiotest_list(bank);
	}

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
		uint start = 0, end = MT7628_GPIO_MAX - 1;
		int interval = 500;

		if (argc >= 3)
			start = dectoul(argv[2], NULL);
		if (argc >= 4)
			end = dectoul(argv[3], NULL);
		if (argc >= 5)
			interval = dectoul(argv[4], NULL);
		return do_gpiotest_all_leds(start, end, interval);
	}

	printf("Unknown subcommand: %s\n", subcmd);
	return CMD_RET_USAGE;
}

U_BOOT_CMD(
	gpiotest, 6, 0, do_gpiotest,
	"GPIO hardware verification tool",
	"list [bank]                 - Scan GPIO pins, show states\n"
	"blink <pin> [cnt] [ms]       - Blink GPIO for LED identification\n"
	"monitor <pin>                - Monitor GPIO input for button identification\n"
	"all-leds [start] [end] [ms]  - Cycle through GPIO pins one by one\n"
	"\n"
	"Examples for MW305R v9.0 GPIO mapping:\n"
	"  gpiotest list              - Show all 96 GPIO states\n"
	"  gpiotest list 1            - Show Bank 1 only (GPIO#32~#63)\n"
	"  gpiotest blink 44 5 300    - Blink GPIO#44 (WLAN LED) 5x @ 300ms\n"
	"  gpiotest monitor 38        - Monitor GPIO#38 (likely RESET button)\n"
	"  gpiotest all-leds 32 46 500  - Cycle GPIO#32~#46 to find LED pin\n"
	"\n"
	"MT7628 GPIO layout: Bank 0 (0-31), Bank 1 (32-63), Bank 2 (64-95)\n"
	"Known pins on MW305R: GPIO#44(WLAN LED), GPIO#38(RESET), GPIO#11(WPS)"
);

/* ==================== ease-of-use shortcuts ==================== */

static int do_gpioscan(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	char *my_argv[4] = {"gpiotest", "list", NULL};

	if (argc >= 2) {
		/* gpioscan <bank> -> gpiotest list <bank> */
		my_argv[2] = argv[1];
		return do_gpiotest(cmdtp, flag, 3, my_argv);
	}

	return do_gpiotest(cmdtp, flag, 2, my_argv);
}

U_BOOT_CMD(
	gpioscan, 2, 0, do_gpioscan,
	"Shortcut: scan all GPIO pins",
	"[bank]  - gpiotest list [bank]"
);
