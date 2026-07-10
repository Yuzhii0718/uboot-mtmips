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
 *
 * Goals:
 * - Provide IP/netmask/gateway/DNS to a directly connected PC
 * - Auto-start with web failsafe (httpd)
 * - Small and self-contained
 */

#include <command.h>
#include <env.h>
#include <net.h>
#include <vsprintf.h>

#include <net/mtk_dhcpd.h>

#define DHCPD_SERVER_PORT	67
#define DHCPD_CLIENT_PORT	68

/*
 * BOOTP/DHCP has a historical minimum message size of 300 bytes.
 * Some clients (notably Windows) may ignore shorter replies.
 */
#define DHCPD_MIN_BOOTP_LEN	300

/* BOOTP/DHCP message header (RFC 2131) */
struct dhcpd_pkt {
	u8 op;
	u8 htype;
	u8 hlen;
	u8 hops;
	u32 xid;
	u16 secs;
	u16 flags;
	u32 ciaddr;
	u32 yiaddr;
	u32 siaddr;
	u32 giaddr;
	u8 chaddr[16];
	u8 sname[64];
	u8 file[128];
	u8 vend[312];
} __packed;

#define BOOTREQUEST		1
#define BOOTREPLY		2

#define HTYPE_ETHER		1
#define HLEN_ETHER		6

#define DHCPDISCOVER	1
#define DHCPOFFER		2
#define DHCPREQUEST		3
#define DHCPNAK			6
#define DHCPACK			5
#define DHCPDECLINE		4
#define DHCPRELEASE		7
#define DHCPINFORM		8

#define DHCP_OPTION_PAD			0
#define DHCP_OPTION_SUBNET_MASK	1
#define DHCP_OPTION_ROUTER		3
#define DHCP_OPTION_DNS_SERVER	6
#define DHCP_OPTION_REQ_IPADDR	50
#define DHCP_OPTION_LEASE_TIME	51
#define DHCP_OPTION_MSG_TYPE	53
#define DHCP_OPTION_SERVER_ID	54
#define DHCP_OPTION_MESSAGE		56
#define DHCP_OPTION_END			255

#define DHCP_FLAG_BROADCAST		0x8000

#define DHCPD_DEFAULT_IP_STR	"192.168.1.1"
#define DHCPD_DEFAULT_NETMASK_STR "255.255.255.0"
#define DHCPD_DEFAULT_POOL_START_HOST	100
#define DHCPD_DEFAULT_POOL_SIZE		101

#define DHCPD_MAX_CLIENTS	8

static const u8 dhcp_magic_cookie[4] = { 99, 130, 83, 99 };

struct dhcpd_lease {
	bool used;
	u8 mac[6];
	struct in_addr ip;
};

static struct dhcpd_lease leases[DHCPD_MAX_CLIENTS];

static rxhand_f *prev_udp_handler;
static bool dhcpd_running;

static bool dhcpd_is_verbose(void)
{
	const char *val = env_get("dhcpd_verbose");

	if (!val || !val[0])
		return false;

	return !strcmp(val, "1") ||
	       !strcasecmp(val, "true") ||
	       !strcasecmp(val, "yes") ||
	       !strcasecmp(val, "on");
}

#define dhcpd_log(fmt, ...) \
	do { if (dhcpd_is_verbose()) printf(fmt, ##__VA_ARGS__); } while (0)

static struct in_addr dhcpd_get_server_ip(void)
{
#ifdef CONFIG_MTK_DHCPD_USE_CONFIG_IP
	const char *env_ip = env_get("ipaddr");

	if (env_ip && env_ip[0])
		return string_to_ip(env_ip);

	return string_to_ip(CONFIG_IPADDR);
#else
	if (net_ip.s_addr)
		return net_ip;

	return string_to_ip(DHCPD_DEFAULT_IP_STR);
#endif
}

static struct in_addr dhcpd_get_netmask(void)
{
#ifdef CONFIG_MTK_DHCPD_USE_CONFIG_IP
	const char *env_nm = env_get("netmask");

	if (env_nm && env_nm[0])
		return string_to_ip(env_nm);

	return string_to_ip(CONFIG_NETMASK);
#else
	if (net_netmask.s_addr)
		return net_netmask;

	return string_to_ip(DHCPD_DEFAULT_NETMASK_STR);
#endif
}

static struct in_addr dhcpd_get_gateway(void)
{
	if (net_gateway.s_addr)
		return net_gateway;

	return dhcpd_get_server_ip();
}

static struct in_addr dhcpd_get_dns(void)
{
	if (net_dns_server.s_addr)
		return net_dns_server;

	return dhcpd_get_server_ip();
}

static u32 dhcpd_get_pool_start_host(void)
{
	const char *val = env_get("dhcpd_pool_start");

	if (val && val[0])
		return (u32)simple_strtoul(val, NULL, 0);

#ifdef CONFIG_MTK_DHCPD_USE_CONFIG_IP
	return (u32)CONFIG_MTK_DHCPD_POOL_START_HOST;
#else
	return DHCPD_DEFAULT_POOL_START_HOST;
#endif
}

static u32 dhcpd_get_pool_size(void)
{
	const char *val = env_get("dhcpd_pool_size");

	if (val && val[0])
		return (u32)simple_strtoul(val, NULL, 0);

#ifdef CONFIG_MTK_DHCPD_USE_CONFIG_IP
	return (u32)CONFIG_MTK_DHCPD_POOL_SIZE;
#else
	return DHCPD_DEFAULT_POOL_SIZE;
#endif
}

static void dhcpd_get_pool_range(u32 *start, u32 *end)
{
	struct in_addr server_ip = dhcpd_get_server_ip();
	struct in_addr netmask = dhcpd_get_netmask();
	u32 ip_host = ntohl(server_ip.s_addr);
	u32 mask = ntohl(netmask.s_addr);
	u32 host_mask = ~mask;
	u32 host_start = dhcpd_get_pool_start_host();
	u32 size = dhcpd_get_pool_size();
	u32 net = ip_host & mask;
	u32 s, e, max_host;

	if (!size)
		size = 1;

	max_host = host_mask;
	host_start &= host_mask;
	if (host_start == 0)
		host_start = 1;

	s = net | host_start;
	e = s + size - 1;

	if ((e & mask) != net || e > (net | max_host))
		e = net | max_host;

	if (start)
		*start = s;
	if (end)
		*end = e;
}

static bool dhcpd_mac_equal(const u8 *a, const u8 *b)
{
	return memcmp(a, b, 6) == 0;
}

static struct dhcpd_lease *dhcpd_find_lease(const u8 *mac)
{
	int i;

	for (i = 0; i < DHCPD_MAX_CLIENTS; i++) {
		if (leases[i].used && dhcpd_mac_equal(leases[i].mac, mac))
			return &leases[i];
	}

	return NULL;
}

static bool dhcpd_ip_in_pool(u32 ip_host)
{
	u32 start, end;

	dhcpd_get_pool_range(&start, &end);

	return ip_host >= start && ip_host <= end;
}

static bool dhcpd_ip_is_allocated(u32 ip_host)
{
	int i;

	for (i = 0; i < DHCPD_MAX_CLIENTS; i++) {
		if (!leases[i].used)
			continue;
		if (ntohl(leases[i].ip.s_addr) == ip_host)
			return true;
	}

	return false;
}

static bool dhcpd_ip_allocated_to_mac(u32 ip_host, const u8 *mac)
{
	int i;

	for (i = 0; i < DHCPD_MAX_CLIENTS; i++) {
		if (!leases[i].used)
			continue;
		if (ntohl(leases[i].ip.s_addr) != ip_host)
			continue;
		return dhcpd_mac_equal(leases[i].mac, mac);
	}

	return false;
}

static u32 dhcpd_mac_hash(const u8 *mac)
{
	u32 h = 2166136261u;
	int i;

	for (i = 0; i < 6; i++) {
		h ^= mac[i];
		h *= 16777619u;
	}

	return h;
}

static struct in_addr dhcpd_alloc_ip(const u8 *mac)
{
	struct dhcpd_lease *l;
	u32 start, end;
	int i;
	struct in_addr ip;
	u32 pool_size;

	l = dhcpd_find_lease(mac);
	if (l && dhcpd_ip_in_pool(ntohl(l->ip.s_addr))) {
		dhcpd_log("DHCP alloc: %pM already has lease %pI4\n",
			  mac, &l->ip);
		return l->ip;
	}

	dhcpd_get_pool_range(&start, &end);

	pool_size = end >= start ? (end - start + 1) : 0;

	if (pool_size) {
		u32 hash = dhcpd_mac_hash(mac);
		u32 off = hash % pool_size;

		for (i = 0; i < (int)pool_size; i++) {
			u32 cand = start + ((off + i) % pool_size);
			if (!dhcpd_ip_is_allocated(cand)) {
				ip.s_addr = htonl(cand);
				return ip;
			}
		}
	}

	/* No free slot: just return the first address in pool */
	ip.s_addr = htonl(start);
	return ip;
}

static u8 dhcpd_parse_msg_type(const struct dhcpd_pkt *bp, unsigned int len)
{
	unsigned int fixed = offsetof(struct dhcpd_pkt, vend);
	const u8 *opt;
	unsigned int optlen;

	if (len < fixed + 4)
		return 0;

	opt = (const u8 *)bp->vend;
	optlen = len - fixed;

	if (memcmp(opt, dhcp_magic_cookie, sizeof(dhcp_magic_cookie)))
		return 0;

	opt += 4;
	optlen -= 4;

	while (optlen) {
		u8 code;
		u8 olen;

		code = *opt++;
		optlen--;

		if (code == DHCP_OPTION_PAD)
			continue;
		if (code == DHCP_OPTION_END)
			break;

		if (!optlen)
			break;
		olen = *opt++;
		optlen--;

		if (olen > optlen)
			break;

		if (code == DHCP_OPTION_MSG_TYPE) {
			if (olen >= 1)
				return opt[0];
		}

		opt += olen;
		optlen -= olen;
	}

	return 0;
}

static bool dhcpd_parse_req_ip(const struct dhcpd_pkt *bp, unsigned int len,
			      struct in_addr *req_ip)
{
	unsigned int fixed = offsetof(struct dhcpd_pkt, vend);
	const u8 *opt;
	unsigned int optlen;

	if (len < fixed + 4)
		return false;

	opt = (const u8 *)bp->vend;
	optlen = len - fixed;

	if (memcmp(opt, dhcp_magic_cookie, sizeof(dhcp_magic_cookie)))
		return false;

	opt += 4;
	optlen -= 4;

	while (optlen) {
		u8 code;
		u8 olen;

		code = *opt++;
		optlen--;

		if (code == DHCP_OPTION_PAD)
			continue;
		if (code == DHCP_OPTION_END)
			break;

		if (!optlen)
			break;
		olen = *opt++;
		optlen--;

		if (olen > optlen)
			break;

		if (code == DHCP_OPTION_REQ_IPADDR && olen == 4) {
			memcpy(&req_ip->s_addr, opt, 4);
			return true;
		}

		opt += olen;
		optlen -= olen;
	}

	return false;
}

static bool dhcpd_parse_server_id(const struct dhcpd_pkt *bp, unsigned int len,
			      struct in_addr *server_ip)
{
	unsigned int fixed = offsetof(struct dhcpd_pkt, vend);
	const u8 *opt;
	unsigned int optlen;

	if (len < fixed + 4)
		return false;

	opt = (const u8 *)bp->vend;
	optlen = len - fixed;

	if (memcmp(opt, dhcp_magic_cookie, sizeof(dhcp_magic_cookie)))
		return false;

	opt += 4;
	optlen -= 4;

	while (optlen) {
		u8 code;
		u8 olen;

		code = *opt++;
		optlen--;

		if (code == DHCP_OPTION_PAD)
			continue;
		if (code == DHCP_OPTION_END)
			break;

		if (!optlen)
			break;
		olen = *opt++;
		optlen--;

		if (olen > optlen)
			break;

		if (code == DHCP_OPTION_SERVER_ID && olen == 4) {
			memcpy(&server_ip->s_addr, opt, 4);
			return true;
		}

		opt += olen;
		optlen -= olen;
	}

	return false;
}

static void dhcpd_process_lease(const u8 *mac, struct in_addr ip)
{
	struct dhcpd_lease *l;
	int i;

	l = dhcpd_find_lease(mac);
	if (l) {
		l->ip = ip;
		return;
	}

	for (i = 0; i < DHCPD_MAX_CLIENTS; i++) {
		if (!leases[i].used) {
			leases[i].used = true;
			memcpy(leases[i].mac, mac, 6);
			leases[i].ip = ip;
			return;
		}
	}

	/* Fallback: replace the first entry */
	leases[0].used = true;
	memcpy(leases[0].mac, mac, 6);
	leases[0].ip = ip;
}

static bool dhcpd_same_subnet(struct in_addr a, struct in_addr b,
			      struct in_addr mask)
{
	return (a.s_addr & mask.s_addr) == (b.s_addr & mask.s_addr);
}

static u8 *dhcpd_opt_add_u8(u8 *p, u8 code, u8 val)
{
	*p++ = code;
	*p++ = 1;
	*p++ = val;
	return p;
}

static u8 *dhcpd_opt_add_u32(u8 *p, u8 code, __be32 val)
{
	*p++ = code;
	*p++ = 4;
	memcpy(p, &val, 4);
	return p + 4;
}

static u8 *dhcpd_opt_add_inaddr(u8 *p, u8 code, struct in_addr addr)
{
	return dhcpd_opt_add_u32(p, code, addr.s_addr);
}

static int dhcpd_send_reply(const struct dhcpd_pkt *req, unsigned int req_len,
			    u8 dhcp_msg_type, struct in_addr yiaddr,
			    const char *nak_message)
{
	struct dhcpd_pkt *bp;
	struct in_addr server_ip, netmask, gw, dns;
	struct in_addr bcast;
	uchar *pkt;
	uchar *payload;
	int eth_hdr_size;
	u8 *opt;
	int payload_len;
	__be32 lease;

	(void)req_len;

	server_ip = dhcpd_get_server_ip();
	netmask = dhcpd_get_netmask();
	gw = dhcpd_get_gateway();
	dns = dhcpd_get_dns();

	bcast.s_addr = 0xFFFFFFFF;

	pkt = net_tx_packet;
	eth_hdr_size = net_set_ether(pkt, net_bcast_ethaddr, PROT_IP);
	net_set_udp_header(pkt + eth_hdr_size, bcast,
			   DHCPD_CLIENT_PORT, DHCPD_SERVER_PORT, 0);

	payload = pkt + eth_hdr_size + IP_UDP_HDR_SIZE;
	bp = (struct dhcpd_pkt *)payload;
	memset(bp, 0, sizeof(*bp));

	bp->op = BOOTREPLY;
	bp->htype = HTYPE_ETHER;
	bp->hlen = HLEN_ETHER;
	bp->hops = 0;
	bp->xid = req->xid;
	bp->secs = req->secs;
	bp->flags = htons(DHCP_FLAG_BROADCAST);
	bp->ciaddr = 0;
	bp->yiaddr = yiaddr.s_addr;
	bp->siaddr = server_ip.s_addr;
	bp->giaddr = 0;
	memcpy(bp->chaddr, req->chaddr, sizeof(bp->chaddr));

	opt = (u8 *)bp->vend;
	memcpy(opt, dhcp_magic_cookie, sizeof(dhcp_magic_cookie));
	opt += 4;

	opt = dhcpd_opt_add_u8(opt, DHCP_OPTION_MSG_TYPE, dhcp_msg_type);
	opt = dhcpd_opt_add_inaddr(opt, DHCP_OPTION_SERVER_ID, server_ip);

	if (dhcp_msg_type != DHCPNAK) {
		opt = dhcpd_opt_add_inaddr(opt, DHCP_OPTION_SUBNET_MASK, netmask);
		opt = dhcpd_opt_add_inaddr(opt, DHCP_OPTION_ROUTER, gw);
		opt = dhcpd_opt_add_inaddr(opt, DHCP_OPTION_DNS_SERVER, dns);

		lease = htonl(3600);
		opt = dhcpd_opt_add_u32(opt, DHCP_OPTION_LEASE_TIME, lease);
	} else if (nak_message && *nak_message) {
		size_t msg_len = strlen(nak_message);
		u8 len = msg_len > 240 ? 240 : (u8)msg_len;

		*opt++ = DHCP_OPTION_MESSAGE;
		*opt++ = len;
		memcpy(opt, nak_message, len);
		opt += len;
	}

	*opt++ = DHCP_OPTION_END;

	payload_len = (int)((uintptr_t)opt - (uintptr_t)payload);
	if (payload_len < DHCPD_MIN_BOOTP_LEN)
		payload_len = DHCPD_MIN_BOOTP_LEN;

	/* Update UDP header with actual payload length */
	net_set_udp_header(pkt + eth_hdr_size, bcast,
			   DHCPD_CLIENT_PORT, DHCPD_SERVER_PORT, payload_len);

	net_send_packet(pkt, eth_hdr_size + IP_UDP_HDR_SIZE + payload_len);

	dhcpd_log("DHCP %s to %pM yiaddr=%pI4 siaddr=%pI4\n",
		  dhcp_msg_type == DHCPOFFER ? "OFFER" :
		  dhcp_msg_type == DHCPACK   ? "ACK"   :
		  dhcp_msg_type == DHCPNAK   ? "NAK"   : "?",
		  req->chaddr, &yiaddr, &server_ip);

	return 0;
}

static void dhcpd_handle_packet(uchar *pkt, unsigned int dport,
			       struct in_addr sip, unsigned int sport,
			       unsigned int len)
{
	const struct dhcpd_pkt *bp = (const struct dhcpd_pkt *)pkt;
	u8 msg_type;
	struct in_addr yiaddr;
	struct in_addr req_ip;

	(void)sip;

	if (!dhcpd_running)
		return;

	if (dport != DHCPD_SERVER_PORT || sport != DHCPD_CLIENT_PORT)
		return;

	if (len < offsetof(struct dhcpd_pkt, vend))
		return;

	if (bp->op != BOOTREQUEST)
		return;

	if (bp->htype != HTYPE_ETHER || bp->hlen != HLEN_ETHER)
		return;

	msg_type = dhcpd_parse_msg_type(bp, len);
	if (!msg_type)
		return;

	dhcpd_log("DHCP %s from %pM xid=0x%08x",
		  msg_type == DHCPDISCOVER ? "DISCOVER" :
		  msg_type == DHCPREQUEST  ? "REQUEST"  :
		  msg_type == DHCPDECLINE  ? "DECLINE"  :
		  msg_type == DHCPRELEASE  ? "RELEASE"  :
		  msg_type == DHCPINFORM   ? "INFORM"   : "?",
		  bp->chaddr, ntohl(bp->xid));
	dhcpd_log(" flags=0x%04x ciaddr=%pI4\n",
		  ntohs(bp->flags), &bp->ciaddr);

	debug_cond(DEBUG_DEV_PKT, "dhcpd: msg=%u from %pM\n", msg_type, bp->chaddr);

	switch (msg_type) {
	case DHCPDISCOVER:
		yiaddr = dhcpd_alloc_ip(bp->chaddr);
		debug_cond(DEBUG_DEV_PKT, "dhcpd: offer %pI4\n", &yiaddr);
		dhcpd_send_reply(bp, len, DHCPOFFER, yiaddr, NULL);
		break;
	case DHCPREQUEST:
		{
			struct in_addr server_id;
			struct in_addr server_ip = dhcpd_get_server_ip();
			struct in_addr netmask = dhcpd_get_netmask();
			bool has_server_id;
			bool has_req_ip;
			u32 ip_host;
			struct in_addr zero_ip;

			zero_ip.s_addr = 0;
			has_server_id = dhcpd_parse_server_id(bp, len, &server_id);
			if (has_server_id && server_id.s_addr != server_ip.s_addr)
				return;

			has_req_ip = dhcpd_parse_req_ip(bp, len, &req_ip);
			if (!has_req_ip) {
				yiaddr = dhcpd_alloc_ip(bp->chaddr);
				dhcpd_process_lease(bp->chaddr, yiaddr);
				dhcpd_send_reply(bp, len, DHCPACK, yiaddr, NULL);
				break;
			}

			ip_host = ntohl(req_ip.s_addr);
			if (!dhcpd_same_subnet(req_ip, server_ip, netmask)) {
				dhcpd_send_reply(bp, len, DHCPNAK, zero_ip, "bad subnet");
				break;
			}
			if (!dhcpd_ip_in_pool(ip_host)) {
				dhcpd_send_reply(bp, len, DHCPNAK, zero_ip, "outside pool");
				break;
			}
			if (dhcpd_ip_is_allocated(ip_host) &&
			    !dhcpd_ip_allocated_to_mac(ip_host, bp->chaddr)) {
				dhcpd_send_reply(bp, len, DHCPNAK, zero_ip, "in use");
				break;
			}

			yiaddr = req_ip;
			dhcpd_process_lease(bp->chaddr, yiaddr);
			dhcpd_send_reply(bp, len, DHCPACK, yiaddr, NULL);
		}
		break;
	default:
		break;
	}
}

static void dhcpd_udp_handler(uchar *pkt, unsigned int dport,
			     struct in_addr sip, unsigned int sport,
			     unsigned int len)
{
	dhcpd_handle_packet(pkt, dport, sip, sport, len);

	if (prev_udp_handler)
		prev_udp_handler(pkt, dport, sip, sport, len);
}

int mtk_dhcpd_start(void)
{
	struct in_addr pool_start;
	u32 pool_start_host, pool_end_host;

	/*
	 * Be robust against net_init()/net_clear_handlers() resetting handlers.
	 * If we're already running but the UDP handler is no longer ours, re-hook.
	 */
	if (dhcpd_running) {
		rxhand_f *cur = net_get_udp_handler();

		if (cur != dhcpd_udp_handler) {
			prev_udp_handler = cur;
			net_set_udp_handler(dhcpd_udp_handler);
		}
		return 0;
	}

	/* Ensure we have a usable local IP, otherwise UDP replies will use 0.0.0.0 */
	if (!net_ip.s_addr)
		net_ip = dhcpd_get_server_ip();
	if (!net_netmask.s_addr)
		net_netmask = dhcpd_get_netmask();
	if (!net_gateway.s_addr)
		net_gateway = net_ip;
	if (!net_dns_server.s_addr)
		net_dns_server = net_ip;

	memset(leases, 0, sizeof(leases));

	dhcpd_get_pool_range(&pool_start_host, &pool_end_host);
	pool_start.s_addr = htonl(pool_start_host);

	prev_udp_handler = net_get_udp_handler();
	net_set_udp_handler(dhcpd_udp_handler);

	dhcpd_running = true;

	dhcpd_log("DHCP server started\n");
	dhcpd_log("  Server IP  : %pI4\n", &net_ip);
	dhcpd_log("  Netmask    : %pI4\n", &net_netmask);
	dhcpd_log("  Gateway    : %pI4\n", &net_gateway);
	dhcpd_log("  DNS        : %pI4\n", &net_dns_server);
	dhcpd_log("  Pool       : %d.%d.%d.%d - %d.%d.%d.%d\n",
		  (pool_start_host >> 24) & 0xff,
		  (pool_start_host >> 16) & 0xff,
		  (pool_start_host >> 8) & 0xff,
		  pool_start_host & 0xff,
		  (pool_end_host >> 24) & 0xff,
		  (pool_end_host >> 16) & 0xff,
		  (pool_end_host >> 8) & 0xff,
		  pool_end_host & 0xff);
	dhcpd_log("  Leases     : %d max\n", DHCPD_MAX_CLIENTS);
	dhcpd_log("  Verbose    : on (setenv dhcpd_verbose 0 to disable)\n");

	return 0;
}

void mtk_dhcpd_stop(void)
{
	if (!dhcpd_running)
		return;

	/*
	 * If the network loop already cleared handlers, don't resurrect another
	 * handler here. We only restore the previous handler if we are still
	 * installed.
	 */
	if (net_get_udp_handler() == dhcpd_udp_handler)
		net_set_udp_handler(prev_udp_handler);
	prev_udp_handler = NULL;
	dhcpd_running = false;

	dhcpd_log("DHCP server stopped\n");
}

bool mtk_dhcpd_is_running(void)
{
	return dhcpd_running;
}

static int do_dhcpd(struct cmd_tbl *cmdtp, int flag, int argc,
		    char *const argv[])
{
	if (argc < 2)
		return CMD_RET_USAGE;

	if (!strcmp(argv[1], "start")) {
		if (mtk_dhcpd_start())
			printf("Failed to start DHCP server\n");

		return CMD_RET_SUCCESS;
	}

	if (!strcmp(argv[1], "stop")) {
		mtk_dhcpd_stop();
		return CMD_RET_SUCCESS;
	}

	return CMD_RET_USAGE;
}

U_BOOT_CMD(dhcpd, 2, 0, do_dhcpd,
	"Control DHCP server",
	"start - start DHCP server\n"
	"dhcpd stop - stop DHCP server\n\n"
	"Environment:\n"
	"  dhcpd_verbose      - set to 1/true/yes/on to enable detailed console output\n"
	"                       (pool info, every DHCP request/reply, lease allocation)\n"
	"  dhcpd_pool_start   - first host index of the DHCP pool (decimal or 0x..)\n"
	"  dhcpd_pool_size    - number of addresses in the DHCP pool"
);
