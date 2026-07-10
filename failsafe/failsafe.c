/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 *
 * Failsafe Web UI
 */

#include <command.h>
#include <errno.h>
#include <malloc.h>
#include <net.h>
#include <net/mtk_tcp.h>
#include <net/mtk_httpd.h>
#include <u-boot/md5.h>
#include <vsprintf.h>
#include "fs.h"

#ifdef CONFIG_MTK_DHCPD
#include <net/mtk_dhcpd.h>
#endif

#include <fdt_support.h>
#include <image.h>

static u32 upload_data_id;
static const void *upload_data;
static size_t upload_size;
static int upgrade_success;
static char update_type[8] = "fw";
static u32 update_type_id;

#ifdef CONFIG_WEBUI_FAILSAFE_INITRAMFS
static bool initramfs_loaded;
static const void *initramfs_data;
static size_t initramfs_size;

static bool initramfs_validate(const void *data, size_t size)
{
	/* Accept FIT image (FDT header) or legacy uImage */
	if (!data || size < sizeof(struct legacy_img_hdr))
		return false;

	if (fdt_magic(data) == FDT_MAGIC)
		return true;

	if (image_check_magic(data))
		return true;

	return false;
}
#endif

int __weak failsafe_validate_image(const void *data, size_t size)
{
	return 0;
}

int __weak failsafe_write_image(const void *data, size_t size)
{
	return -ENOSYS;
}

int __weak failsafe_validate_uboot(const void *data, size_t size)
{
	return -ENOSYS;
}

int __weak failsafe_write_uboot(const void *data, size_t size)
{
	return -ENOSYS;
}

static int output_plain_file(struct httpd_response *response,
			     const char *filename)
{
	const struct fs_desc *file;
	int ret = 0;

	file = fs_find_file(filename);

	response->status = HTTP_RESP_STD;

	if (file) {
		response->data = file->data;
		response->size = file->size;
	} else {
		response->data = "Error: file not found";
		response->size = strlen(response->data);
		ret = 1;
	}

	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/html";

	return ret;
}

static void index_handler(enum httpd_uri_handler_status status,
			  struct httpd_request *request,
			  struct httpd_response *response)
{
	if (status == HTTP_CB_NEW)
		output_plain_file(response, "index.html");
}

struct upload_status {
	bool free_response_data;
};

static void upload_handler(enum httpd_uri_handler_status status,
			  struct httpd_request *request,
			  struct httpd_response *response)
{
	char *buff, *md5_ptr, *size_ptr, *type_ptr, size_str[16];
	struct httpd_form_value *fw, *ut;
	struct upload_status *us;
	u8 md5_sum[16];
	const char *type_name;
	int i;

	static char hexchars[] = "0123456789abcdef";

	if (status == HTTP_CB_NEW) {
		us = calloc(1, sizeof(*us));
		if (!us) {
			response->info.code = 500;
			return;
		}

		response->session_data = us;

		fw = httpd_request_find_value(request, "firmware");

#ifdef CONFIG_WEBUI_FAILSAFE_INITRAMFS
		/* Check for initramfs upload */
		if (!fw) {
			fw = httpd_request_find_value(request, "initramfs");
			if (fw) {
				if (!initramfs_validate(fw->data, fw->size)) {
					if (output_plain_file(response, "validate_fail.html"))
						response->info.code = 500;
					return;
				}

				initramfs_data = fw->data;
				initramfs_size = fw->size;

				strcpy(update_type, "initramfs");
				update_type_id = upload_id;
				upload_data_id = upload_id;
				upload_data = fw->data;
				upload_size = fw->size;
				type_name = "Initramfs";

				goto show_confirm;
			}
		}
#endif

		if (!fw) {
			response->info.code = 302;
			response->info.connection_close = 1;
			response->info.location = "/";
			return;
		}

		/* Determine update type */
		ut = httpd_request_find_value(request, "update_type");
		update_type_id = upload_id;
		if (ut && ut->data && !strcmp(ut->data, "bl")) {
			strcpy(update_type, "bl");
			if (failsafe_validate_uboot(fw->data, fw->size)) {
				if (output_plain_file(response, "validate_fail.html"))
					response->info.code = 500;

				return;
			}
			type_name = "U-Boot / Bootloader";
		} else {
			strcpy(update_type, "fw");
			if (failsafe_validate_image(fw->data, fw->size)) {
				if (output_plain_file(response, "validate_fail.html"))
					response->info.code = 500;

				return;
			}
			type_name = "Firmware";
		}

show_confirm:
		if (output_plain_file(response, "upload.html")) {
			response->info.code = 500;
			return;
		}

		buff = malloc(response->size + 1);
		if (buff) {
			memcpy(buff, response->data, response->size);
			buff[response->size] = 0;

			md5_ptr = strstr(buff, "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX");
			size_ptr = strstr(buff, "YYYYYYYYYY");
			type_ptr = strstr(buff, "ZZZZZZZZZZZZZZZZ");

			if (md5_ptr) {
				md5_wd((u8 *)fw->data, fw->size, md5_sum, MD5_DEF_CHUNK_SZ);
				for (i = 0; i < 16; i++) {
					u8 hex;

					hex = (md5_sum[i] >> 4) & 0xf;
					md5_ptr[i * 2] = hexchars[hex];
					hex = md5_sum[i] & 0xf;
					md5_ptr[i * 2 + 1] = hexchars[hex];
				}
			}

			if (size_ptr) {
				u32 n;

				n = snprintf(size_str, sizeof(size_str), "%zu",
					     fw->size);
				memset(size_str + n, ' ', sizeof(size_str) - n);
				memcpy(size_ptr, size_str, 10);
			}

			if (type_ptr) {
				size_t tlen = strlen(type_name);

				memcpy(type_ptr, type_name, tlen);
				if (tlen < 16)
					memset(type_ptr + tlen, ' ', 16 - tlen);
			}

			response->data = buff;
			us->free_response_data = true;
		}

		upload_data_id = upload_id;
		upload_data = fw->data;
		upload_size = fw->size;

		return;
	}

	if (status == HTTP_CB_CLOSED) {
		if (response->session_data) {
			us = response->session_data;

			if (us->free_response_data)
				free((void *)response->data);

			free(response->session_data);
		}
	}
}

static void flashing_handler(enum httpd_uri_handler_status status,
			     struct httpd_request *request,
			     struct httpd_response *response)
{
	if (status == HTTP_CB_NEW)
		output_plain_file(response, "flashing.html");
}

struct flashing_status {
	char buf[4096];
	int ret;
	int body_sent;
};

static void result_handler(enum httpd_uri_handler_status status,
			  struct httpd_request *request,
			  struct httpd_response *response)
{
	const struct fs_desc *file;
	struct flashing_status *st;
	u32 size;

	if (status == HTTP_CB_NEW) {
		st = calloc(1, sizeof(*st));
		if (!st) {
			response->info.code = 500;
			return;
		}

		st->ret = -1;

		response->session_data = st;

		response->status = HTTP_RESP_CUSTOM;

		response->info.http_1_0 = 1;
		response->info.content_length = -1;
		response->info.connection_close = 1;
		response->info.content_type = "text/html";
		response->info.code = 200;

		size = http_make_response_header(&response->info,
			st->buf, sizeof(st->buf));

		response->data = st->buf;
		response->size = size;

		return;
	}

	if (status == HTTP_CB_RESPONDING) {
		st = response->session_data;

		if (st->body_sent) {
			response->status = HTTP_RESP_NONE;
			return;
		}

		if (upload_data_id == upload_id) {
#ifdef CONFIG_WEBUI_FAILSAFE_INITRAMFS
			if (update_type_id == upload_id &&
			    !strcmp(update_type, "initramfs"))
				st->ret = 0; /* initramfs: no flash write needed */
			else
#endif
			if (update_type_id == upload_id &&
			    !strcmp(update_type, "bl"))
				st->ret = failsafe_write_uboot(upload_data,
							       upload_size);
			else
				st->ret = failsafe_write_image(upload_data,
							       upload_size);
		}

		/* invalidate upload identifier */
		upload_data_id = rand();
		update_type_id = rand();

		if (!st->ret)
			file = fs_find_file("success.html");
		else
			file = fs_find_file("fail.html");

		if (!file) {
			if (!st->ret)
				response->data = "Upgrade completed!";
			else
				response->data = "Upgrade failed!";
			response->size = strlen(response->data);
			return;
		}

		response->data = file->data;
		response->size = file->size;

		st->body_sent = 1;

		return;
	}

	if (status == HTTP_CB_CLOSED) {
		st = response->session_data;

		upgrade_success = !st->ret;

		free(response->session_data);

		if (upgrade_success)
			mtk_tcp_close_all_conn();
	}
}

static void style_handler(enum httpd_uri_handler_status status,
			  struct httpd_request *request,
			  struct httpd_response *response)
{
	if (status == HTTP_CB_NEW) {
		output_plain_file(response, "style.css");
		response->info.content_type = "text/css";
	}
}

#ifdef CONFIG_WEBUI_FAILSAFE_INITRAMFS
static void initramfs_page_handler(enum httpd_uri_handler_status status,
			      struct httpd_request *request,
			      struct httpd_response *response)
{
	if (status == HTTP_CB_NEW)
		output_plain_file(response, "initramfs.html");
}

static void booting_page_handler(enum httpd_uri_handler_status status,
			      struct httpd_request *request,
			      struct httpd_response *response)
{
	if (status == HTTP_CB_NEW)
		output_plain_file(response, "booting.html");
}
#endif

static void not_found_handler(enum httpd_uri_handler_status status,
			      struct httpd_request *request,
			      struct httpd_response *response)
{
	if (status == HTTP_CB_NEW) {
		output_plain_file(response, "404.html");
		response->info.code = 404;
	}
}

int start_web_failsafe(void)
{
	struct httpd_instance *inst;

	inst = httpd_find_instance(80);
	if (inst)
		httpd_free_instance(inst);

	inst = httpd_create_instance(80);
	if (!inst) {
		printf("Error: failed to create HTTP instance on port 80\n");
		return -1;
	}

	httpd_register_uri_handler(inst, "/", &index_handler, NULL);
	httpd_register_uri_handler(inst, "/cgi-bin/luci", &index_handler, NULL);
	httpd_register_uri_handler(inst, "/upload", &upload_handler, NULL);
	httpd_register_uri_handler(inst, "/flashing", &flashing_handler, NULL);
	httpd_register_uri_handler(inst, "/result", &result_handler, NULL);
	httpd_register_uri_handler(inst, "/style.css", &style_handler, NULL);
#ifdef CONFIG_WEBUI_FAILSAFE_INITRAMFS
	httpd_register_uri_handler(inst, "/initramfs_upload", &upload_handler, NULL);
	httpd_register_uri_handler(inst, "/initramfs.html", &initramfs_page_handler,
				    NULL);
	httpd_register_uri_handler(inst, "/booting.html", &booting_page_handler,
				    NULL);
#endif
	httpd_register_uri_handler(inst, "", &not_found_handler, NULL);

#ifdef CONFIG_MTK_DHCPD
	mtk_dhcpd_start();
	printf("DHCP server started\n");
#endif

	net_loop(MTK_TCP);

#ifdef CONFIG_MTK_DHCPD
	mtk_dhcpd_stop();
#endif

	return 0;
}

static int do_httpd(struct cmd_tbl *cmdtp, int flag, int argc,
		    char *const argv[])
{
	u32 local_ip = ntohl(net_ip.s_addr);
	int ret;

	printf("\nWeb failsafe UI started\n");
	printf("URL: http://%u.%u.%u.%u/\n",
	       (local_ip >> 24) & 0xff, (local_ip >> 16) & 0xff,
	       (local_ip >> 8) & 0xff, local_ip & 0xff);
	printf("\nPress Ctrl+C to exit\n");

	ret = start_web_failsafe();

	if (upgrade_success) {
#ifdef CONFIG_WEBUI_FAILSAFE_INITRAMFS
		if (initramfs_loaded && initramfs_data) {
			char cmd[64];

			printf("\nBooting initramfs from 0x%08lx (%zd bytes)...\n",
			       (ulong)initramfs_data, initramfs_size);
			snprintf(cmd, sizeof(cmd), "bootm %lx",
				 (ulong)initramfs_data);
			run_command(cmd, 0);
			/* bootm failed, fall through to reset */
		}
#endif
		do_reset(NULL, 0, 0, NULL);
	}

	return ret;
}

U_BOOT_CMD(httpd, 1, 0, do_httpd,
	"Start failsafe HTTP server", ""
);
