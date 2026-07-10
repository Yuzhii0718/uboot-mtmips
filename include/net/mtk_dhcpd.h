// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 *
 * Minimal DHCPv4 server for MediaTek web failsafe.
 */

#ifndef __NET_MTK_DHCPD_H__
#define __NET_MTK_DHCPD_H__

#include <stdbool.h>

int mtk_dhcpd_start(void);
void mtk_dhcpd_stop(void);
bool mtk_dhcpd_is_running(void);

#endif /* __NET_MTK_DHCPD_H__ */
