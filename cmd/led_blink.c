// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * LED blink command (poller-free, serviced by caller)
 */

#include <command.h>
#include <led.h>

#define MAX_LED_BLINK 5

struct led_blink {
	struct udevice *dev;
	int freq_ms;
};

static struct led_blink led_blinks[MAX_LED_BLINK];

void led_blink_start(const char *label, int freq_ms)
{
	struct udevice *dev;
	int i, ret;

	ret = led_get_by_label(label, &dev);
	if (ret)
		return;

	for (i = 0; i < MAX_LED_BLINK; i++) {
		if (led_blinks[i].dev == dev) {
			if (freq_ms < 1) {
				led_blinks[i].dev = NULL;
				led_set_state(dev, LEDST_OFF);
			} else {
				led_blinks[i].freq_ms = freq_ms;
			}
			return;
		}
	}

	if (freq_ms < 1)
		return;

	for (i = 0; i < MAX_LED_BLINK; i++) {
		if (!led_blinks[i].dev) {
			led_blinks[i].dev = dev;
			led_blinks[i].freq_ms = freq_ms;
			return;
		}
	}
}

void led_blink_stop(const char *label)
{
	led_blink_start(label, 0);
}

void led_blink_service(void)
{
	int i;

	for (i = 0; i < MAX_LED_BLINK; i++) {
		if (!led_blinks[i].dev || led_blinks[i].freq_ms < 1)
			continue;
		led_set_state(led_blinks[i].dev, LEDST_TOGGLE);
	}
}

void led_blink_get_min_freq(int *min_freq)
{
	int i, min = 0;

	for (i = 0; i < MAX_LED_BLINK; i++) {
		if (!led_blinks[i].dev || led_blinks[i].freq_ms < 1)
			continue;
		if (min == 0 || led_blinks[i].freq_ms < min)
			min = led_blinks[i].freq_ms;
	}
	*min_freq = min;
}

static int do_led_blink(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	int freq_ms;

	if (argc < 3)
		return CMD_RET_USAGE;

	freq_ms = dectoul(argv[2], NULL);

	if (freq_ms < 1)
		led_blink_stop(argv[1]);
	else
		led_blink_start(argv[1], freq_ms);

	return 0;
}

U_BOOT_CMD(
	ledblink, 3, 0, do_led_blink,
	"led blink",
	"<led_label> [blink-freq in ms]"
);
