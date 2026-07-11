/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718 <admin@yuzhii0718.eu.org>
 *
 * QCA bootmenu helper — defines bootmenu_entry struct and API.
 */

#ifndef _QCA_BOOTMENU_HELPER_H_
#define _QCA_BOOTMENU_HELPER_H_

#include <linux/types.h>
#include <stdbool.h>
#include <stdarg.h>

struct bootmenu_entry {
	const char *desc;
	const char *cmd;
};

void board_bootmenu_entries(const struct bootmenu_entry **menu, u32 *count);
void set_bootmenu_repeat(const char *fmt, ...);

#endif /* _QCA_BOOTMENU_HELPER_H_ */
