/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file fastboot.c
 * @brief Fastboot protocol implementation for TPL bootloader
 */

#include "fastboot.h"
#include "flash_helper.h"
#include "firmware_loader.h"
#include "firmware_header.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

LOG_MODULE_REGISTER(fastboot, CONFIG_LOG_DEFAULT_LEVEL);

/* ============================================================
 * USB Device Context & Descriptors
 * ============================================================ */

USBD_DEVICE_DEFINE(fastboot_usbd,
		   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   FASTBOOT_VID, FASTBOOT_PID);

USBD_DESC_LANG_DEFINE(fastboot_lang);
USBD_DESC_MANUFACTURER_DEFINE(fastboot_mfr, "ART-Pi2");
USBD_DESC_PRODUCT_DEFINE(fastboot_product, "ART-Pi2 TPL Fastboot");

// USBD_DESC_CONFIG_DEFINE(fastboot_fs_cfg_desc, "Fastboot FS Config");
USBD_DESC_CONFIG_DEFINE(fastboot_hs_cfg_desc, "Fastboot HS Config");
// USBD_CONFIGURATION_DEFINE(fastboot_fs_config, 0, 250, &fastboot_fs_cfg_desc);
USBD_CONFIGURATION_DEFINE(fastboot_hs_config, 0, 250, &fastboot_hs_cfg_desc);

/* ============================================================
 * Fastboot Descriptors (must be in RAM, not flash)
 * ============================================================ */

/*
 * Descriptor template - used to init writable copies in RAM.
 * These are const and can stay in flash.
 */
#define FS_BULK_MPS USB_CONTROL_EP_MPS
#define HS_BULK_MPS 512

// static const struct usb_if_descriptor fastboot_fs_iface_tpl = {
// 	.bLength = sizeof(struct usb_if_descriptor),
// 	.bDescriptorType = USB_DESC_INTERFACE,
// 	.bInterfaceNumber = 0,
// 	.bAlternateSetting = 0,
// 	.bNumEndpoints = 2,
// 	.bInterfaceClass = FASTBOOT_IFACE_CLASS,
// 	.bInterfaceSubClass = FASTBOOT_IFACE_SUBCLASS,
// 	.bInterfaceProtocol = FASTBOOT_IFACE_PROTOCOL,
// 	.iInterface = 0,
// };

// static const struct usb_ep_descriptor fastboot_fs_bulk_out_tpl = {
// 	.bLength = sizeof(struct usb_ep_descriptor),
// 	.bDescriptorType = USB_DESC_ENDPOINT,
// 	.bEndpointAddress = FASTBOOT_BULK_OUT_EP,
// 	.bmAttributes = USB_EP_TYPE_BULK,
// 	.wMaxPacketSize = sys_cpu_to_le16(FS_BULK_MPS),
// 	.bInterval = 0x00,
// };

// static const struct usb_ep_descriptor fastboot_fs_bulk_in_tpl = {
// 	.bLength = sizeof(struct usb_ep_descriptor),
// 	.bDescriptorType = USB_DESC_ENDPOINT,
// 	.bEndpointAddress = FASTBOOT_BULK_IN_EP,
// 	.bmAttributes = USB_EP_TYPE_BULK,
// 	.wMaxPacketSize = sys_cpu_to_le16(FS_BULK_MPS),
// 	.bInterval = 0x00,
// };

static const struct usb_if_descriptor fastboot_hs_iface_tpl = {
	.bLength = sizeof(struct usb_if_descriptor),
	.bDescriptorType = USB_DESC_INTERFACE,
	.bInterfaceNumber = 0,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = FASTBOOT_IFACE_CLASS,
	.bInterfaceSubClass = FASTBOOT_IFACE_SUBCLASS,
	.bInterfaceProtocol = FASTBOOT_IFACE_PROTOCOL,
	.iInterface = 0,
};

static const struct usb_ep_descriptor fastboot_hs_bulk_out_tpl = {
	.bLength = sizeof(struct usb_ep_descriptor),
	.bDescriptorType = USB_DESC_ENDPOINT,
	.bEndpointAddress = FASTBOOT_BULK_OUT_EP,
	.bmAttributes = USB_EP_TYPE_BULK,
	.wMaxPacketSize = sys_cpu_to_le16(HS_BULK_MPS),
	.bInterval = 0x00,
};

static const struct usb_ep_descriptor fastboot_hs_bulk_in_tpl = {
	.bLength = sizeof(struct usb_ep_descriptor),
	.bDescriptorType = USB_DESC_ENDPOINT,
	.bEndpointAddress = FASTBOOT_BULK_IN_EP,
	.bmAttributes = USB_EP_TYPE_BULK,
	.wMaxPacketSize = sys_cpu_to_le16(HS_BULK_MPS),
	.bInterval = 0x00,
};

/* RAM copies of descriptors (BSS, in SRAM, writable) */
// static struct {
// 	struct usb_if_descriptor iface;
// 	struct usb_ep_descriptor bulk_out;
// 	struct usb_ep_descriptor bulk_in;
// 	struct usb_desc_header *desc_array[4];
// } fastboot_fs_desc_ram;

static struct {
	struct usb_if_descriptor iface;
	struct usb_ep_descriptor bulk_out;
	struct usb_ep_descriptor bulk_in;
	struct usb_desc_header *desc_array[4];
} fastboot_hs_desc_ram;

/* Pointers for get_desc() to return */
static struct usb_desc_header **fastboot_fs_desc;
static struct usb_desc_header **fastboot_hs_desc;

/* ============================================================
 * Fastboot Context & Class API
 * ============================================================ */

/* Static download buffer allocated from heap on demand */
static struct fastboot_ctx fb_ctx = {
	.state = FASTBOOT_STATE_OFFLINE,
	.download_buf = NULL,
};

static struct usbd_class_data *fb_c_data;

/* Forward declarations */
static void fastboot_process_data(struct usbd_class_data *c_data,
				  const uint8_t *data, size_t len);

/* Forward declarations */
static void fastboot_process_data(struct usbd_class_data *c_data,
				  const uint8_t *data, size_t len);
static void fastboot_init_desc(void);

static void *fastboot_get_desc(struct usbd_class_data *const c_data,
			       const enum usbd_speed speed)
{
	ARG_UNUSED(c_data);

	/* Ensure RAM descriptor copies are initialized before first use */
	fastboot_init_desc();

	if (speed == USBD_SPEED_HS) {
		return (void *)fastboot_hs_desc;
	}
	return (void *)fastboot_fs_desc;
}

static int fastboot_request(struct usbd_class_data *const c_data,
			    struct net_buf *buf, int err)
{
	struct usbd_context *ctx = usbd_class_get_ctx(c_data);

	if (err != 0) {
		printk("fastboot: bulk err %d\n", err);
		usbd_ep_buf_free(ctx, buf);
		return err;
	}

	if (buf->len > 0) {
		fastboot_process_data(c_data, buf->data, buf->len);
	}

	usbd_ep_buf_free(ctx, buf);

	/* Re-submit bulk OUT */
	{
		struct net_buf *new_buf = usbd_ep_buf_alloc(c_data,
					FASTBOOT_BULK_OUT_EP, 64);
		if (new_buf) {
			usbd_ep_enqueue(c_data, new_buf);
		}
	}

	return 0;
}

static void fastboot_enable(struct usbd_class_data *const c_data)
{
	struct net_buf *buf;

	printk("fastboot: enabled\n");
	fb_c_data = c_data;
	fb_ctx.state = FASTBOOT_STATE_IDLE;

	buf = usbd_ep_buf_alloc(c_data, FASTBOOT_BULK_OUT_EP, 64);
	if (buf) {
		usbd_ep_enqueue(c_data, buf);
	}
}

static void fastboot_disable(struct usbd_class_data *const c_data)
{
	printk("fastboot: disabled\n");
	fb_c_data = NULL;
	fb_ctx.state = FASTBOOT_STATE_OFFLINE;
	usbd_ep_dequeue(usbd_class_get_ctx(c_data), FASTBOOT_BULK_OUT_EP);
	usbd_ep_dequeue(usbd_class_get_ctx(c_data), FASTBOOT_BULK_IN_EP);
}

static void fastboot_init_desc(void)
{
	static bool initialized;

	if (initialized) {
		return;
	}

	// memcpy(&fastboot_fs_desc_ram.iface, &fastboot_fs_iface_tpl,
	//        sizeof(fastboot_fs_desc_ram.iface));
	// memcpy(&fastboot_fs_desc_ram.bulk_out, &fastboot_fs_bulk_out_tpl,
	//        sizeof(fastboot_fs_desc_ram.bulk_out));
	// memcpy(&fastboot_fs_desc_ram.bulk_in, &fastboot_fs_bulk_in_tpl,
	//        sizeof(fastboot_fs_desc_ram.bulk_in));
	// fastboot_fs_desc_ram.desc_array[0] = (struct usb_desc_header *)&fastboot_fs_desc_ram.iface;
	// fastboot_fs_desc_ram.desc_array[1] = (struct usb_desc_header *)&fastboot_fs_desc_ram.bulk_out;
	// fastboot_fs_desc_ram.desc_array[2] = (struct usb_desc_header *)&fastboot_fs_desc_ram.bulk_in;
	// fastboot_fs_desc_ram.desc_array[3] = NULL;
	// fastboot_fs_desc = fastboot_fs_desc_ram.desc_array;

	memcpy(&fastboot_hs_desc_ram.iface, &fastboot_hs_iface_tpl,
	       sizeof(fastboot_hs_desc_ram.iface));
	memcpy(&fastboot_hs_desc_ram.bulk_out, &fastboot_hs_bulk_out_tpl,
	       sizeof(fastboot_hs_desc_ram.bulk_out));
	memcpy(&fastboot_hs_desc_ram.bulk_in, &fastboot_hs_bulk_in_tpl,
	       sizeof(fastboot_hs_desc_ram.bulk_in));
	fastboot_hs_desc_ram.desc_array[0] = (struct usb_desc_header *)&fastboot_hs_desc_ram.iface;
	fastboot_hs_desc_ram.desc_array[1] = (struct usb_desc_header *)&fastboot_hs_desc_ram.bulk_out;
	fastboot_hs_desc_ram.desc_array[2] = (struct usb_desc_header *)&fastboot_hs_desc_ram.bulk_in;
	fastboot_hs_desc_ram.desc_array[3] = NULL;
	fastboot_hs_desc = fastboot_hs_desc_ram.desc_array;

	initialized = true;
}

static int fastboot_init_class(struct usbd_class_data *const c_data)
{
	ARG_UNUSED(c_data);
	memset(&fb_ctx, 0, sizeof(fb_ctx));
	fb_ctx.state = FASTBOOT_STATE_OFFLINE;
	printk("fastboot: class init\n");
	return 0;
}

static const struct usbd_class_api fastboot_class_api = {
	.get_desc    = fastboot_get_desc,
	.request     = fastboot_request,
	.enable      = fastboot_enable,
	.disable     = fastboot_disable,
	.init        = fastboot_init_class,
};

USBD_DEFINE_CLASS(fastboot_class, &fastboot_class_api, &fb_ctx, NULL);

/* ============================================================
 * Fastboot Response Helpers
 * ============================================================ */

static int fastboot_send(struct usbd_class_data *c_data,
			 const char *data, size_t len)
{
	struct net_buf *buf;
	if (!c_data) return -ENODEV;
	buf = usbd_ep_buf_alloc(c_data, FASTBOOT_BULK_IN_EP, len);
	if (!buf) return -ENOMEM;
	net_buf_add_mem(buf, data, len);
	return usbd_ep_enqueue(c_data, buf);
}

#define fastboot_okay(c)    fastboot_send((c), "OKAY", 4)
#define fastboot_fail(c,m)  do { \
	char _b[64]; int _n = snprintf(_b, sizeof(_b), "FAIL%s", (m)); \
	fastboot_send((c), _b, _n); } while(0)

/* ============================================================
 * Partition Table
 * ============================================================ */

struct fastboot_part {
	const char *name;
	uint32_t addr;
	uint32_t size;
	uint32_t magic;
	bool internal;
};

static const struct fastboot_part parts[] = {
	{"spl", FLASH_BASE_ADDR, SPL_FLASH_SIZE, FW_MAGIC_SPL, true},
	{"tpl", EXT_FLASH_SLOT0_ADDR, EXT_FLASH_SLOT0_SIZE, FW_MAGIC_TPL, false},
	{"app", EXT_FLASH_SLOT1_ADDR, EXT_FLASH_SLOT1_SIZE, FW_MAGIC_APP, false},
};

static const struct fastboot_part *find_part(const char *name)
{
	for (size_t i = 0; i < ARRAY_SIZE(parts); i++) {
		if (strcmp(parts[i].name, name) == 0) return &parts[i];
	}
	return NULL;
}

/* ============================================================
 * Command Handlers
 * ============================================================ */

static void cmd_getvar(struct usbd_class_data *c, const char *arg)
{
	char buf[64];

	if (strcmp(arg, "version") == 0) {
		fastboot_send(c, "OKAY0.4", 7);
	} else if (strcmp(arg, "board") == 0) {
		fastboot_send(c, "OKAYart-pi2", 12);
	} else if (strcmp(arg, "serial") == 0) {
		fastboot_send(c, "OKAY000000000000", 16);
	} else if (strcmp(arg, "max-download-size") == 0) {
		snprintf(buf, sizeof(buf), "OKAY0x%08x", FASTBOOT_DOWNLOAD_BUF);
		fastboot_send(c, buf, strlen(buf));
	} else if (strcmp(arg, "all") == 0) {
		fastboot_send(c, "INFOversion: 0.4", 16);
		fastboot_send(c, "INFOboard: art-pi2", 17);
		fastboot_send(c, "INFOserial: 000000000000", 22);
		fastboot_send(c, "INFOmax-download-size: 0x8000", 29);
		fastboot_okay(c);
	} else if (strncmp(arg, "partition-size:", 15) == 0) {
		const struct fastboot_part *p = find_part(arg + 15);
		if (p) {
			snprintf(buf, sizeof(buf), "OKAY0x%08x", p->size);
			fastboot_send(c, buf, strlen(buf));
		} else {
			fastboot_fail(c, "no such partition");
		}
	} else if (strncmp(arg, "partition-type:", 15) == 0) {
		const struct fastboot_part *p = find_part(arg + 15);
		if (p) {
			fastboot_send(c, "OKAYraw", 7);
		} else {
			fastboot_fail(c, "no such partition");
		}
	} else {
		fastboot_okay(c);
	}
}

static void cmd_download(struct usbd_class_data *c, const char *arg)
{
	uint32_t size = (uint32_t)strtoul(arg, NULL, 16);

	if (size == 0 || size > FASTBOOT_DOWNLOAD_BUF) {
		fastboot_fail(c, "bad size");
		return;
	}

	if (!fb_ctx.download_buf) {
		fb_ctx.download_buf = k_malloc(FASTBOOT_DOWNLOAD_BUF);
		if (!fb_ctx.download_buf) {
			fastboot_fail(c, "oom");
			return;
		}
	}

	fb_ctx.download_size = size;
	fb_ctx.download_received = 0;
	fb_ctx.state = FASTBOOT_STATE_DOWNLOAD;

	char resp[16];
	snprintf(resp, sizeof(resp), "DATA%08x", size);
	fastboot_send(c, resp, strlen(resp));
}

static void cmd_flash(struct usbd_class_data *c, const char *arg)
{
	const struct fastboot_part *p;
	int rc;

	if (fb_ctx.state != FASTBOOT_STATE_DOWNLOADED) {
		fastboot_fail(c, "no firmware downloaded");
		return;
	}

	p = find_part(arg);
	if (!p) {
		fastboot_fail(c, "no such partition");
		return;
	}

	if (fb_ctx.download_received < sizeof(struct firmware_header)) {
		fastboot_fail(c, "firmware too small");
		return;
	}

	/* Verify header */
	struct firmware_header *hdr = (struct firmware_header *)fb_ctx.download_buf;
	if (firmware_header_verify(hdr, p->magic) != FW_LOADER_SUCCESS) {
		fastboot_fail(c, "bad firmware header");
		return;
	}

	printk("fastboot: erasing %s...\n", p->name);
	if (p->internal) {
		rc = flash_internal_erase(p->addr, p->size);
	} else {
		rc = flash_external_erase(p->addr, p->size);
	}
	if (rc) { fastboot_fail(c, "erase failed"); return; }

	printk("fastboot: writing %s...\n", p->name);
	{
		uint32_t off = 0;
		uint32_t left = fb_ctx.download_received;
		while (left > 0) {
			uint32_t chunk = MIN(left, 256);
			if (p->internal) {
				rc = flash_internal_write(p->addr + off,
					fb_ctx.download_buf + off, chunk);
			} else {
				rc = flash_external_write(p->addr + off,
					fb_ctx.download_buf + off, chunk);
			}
			if (rc) break;
			off += chunk;
			left -= chunk;
		}
	}
	if (rc) { fastboot_fail(c, "write failed"); return; }

	/* TPL: set update request flag */
	if (strcmp(arg, "tpl") == 0) {
		flash_set_update_request(UPDATE_REQ_TPL);
	}

	fb_ctx.state = FASTBOOT_STATE_IDLE;
	k_free(fb_ctx.download_buf);
	fb_ctx.download_buf = NULL;

	fastboot_okay(c);
}

static void cmd_erase(struct usbd_class_data *c, const char *arg)
{
	const struct fastboot_part *p = find_part(arg);
	if (!p) { fastboot_fail(c, "no such partition"); return; }

	int rc;
	if (p->internal) {
		rc = flash_internal_erase(p->addr, p->size);
	} else {
		rc = flash_external_erase(p->addr, p->size);
	}

	if (rc) { fastboot_fail(c, "erase failed"); }
	else    { fastboot_okay(c); }
}

/* ============================================================
 * Command Dispatcher
 * ============================================================ */

static void handle_cmd(struct usbd_class_data *c, const char *cmd)
{
	printk("fastboot: cmd '%s'\n", cmd);

	if (strncmp(cmd, "getvar:", 7) == 0)
		cmd_getvar(c, cmd + 7);
	else if (strncmp(cmd, "download:", 9) == 0)
		cmd_download(c, cmd + 9);
	else if (strncmp(cmd, "flash:", 6) == 0)
		cmd_flash(c, cmd + 6);
	else if (strncmp(cmd, "erase:", 6) == 0)
		cmd_erase(c, cmd + 6);
	else if (strcmp(cmd, "reboot") == 0) {
		fastboot_okay(c);
		k_sleep(K_MSEC(100));
		sys_reboot(SYS_REBOOT_COLD);
	} else if (strcmp(cmd, "continue") == 0)
		fastboot_okay(c);
	else
		fastboot_fail(c, "unknown command");
}

/* ============================================================
 * Data Processor
 * ============================================================ */

static void fastboot_process_data(struct usbd_class_data *c_data,
				  const uint8_t *data, size_t len)
{
	switch (fb_ctx.state) {

	case FASTBOOT_STATE_IDLE: {
		char cmd[64];
		size_t n = MIN(len, sizeof(cmd) - 1);
		memcpy(cmd, data, n);
		cmd[n] = '\0';
		while (n > 0 && (cmd[n-1] == '\r' || cmd[n-1] == '\n'))
			cmd[--n] = '\0';
		if (n > 0) handle_cmd(c_data, cmd);
		break;
	}

	case FASTBOOT_STATE_DOWNLOAD: {
		uint32_t copy = MIN(len,
			fb_ctx.download_size - fb_ctx.download_received);
		if (copy > 0) {
			memcpy(fb_ctx.download_buf + fb_ctx.download_received,
			       data, copy);
			fb_ctx.download_received += copy;
		}
		if (fb_ctx.download_received >= fb_ctx.download_size) {
			fb_ctx.state = FASTBOOT_STATE_DOWNLOADED;
			fastboot_okay(c_data);
		}
		break;
	}

	default:
		fastboot_fail(c_data, "protocol error");
		fb_ctx.state = FASTBOOT_STATE_IDLE;
		break;
	}
}

/* ============================================================
 * VBUS Message Callback
 * ============================================================ */

static void fb_msg_cb(struct usbd_context *const ctx,
		      const struct usbd_msg *msg)
{
	switch (msg->type) {
	case USBD_MSG_VBUS_READY:
		printk("fastboot: VBUS ready\n");
		usbd_enable(ctx);
		break;
	case USBD_MSG_VBUS_REMOVED:
		printk("fastboot: VBUS removed\n");
		usbd_disable(ctx);
		break;
	default:
		break;
	}
}

/* ============================================================
 * Public API
 * ============================================================ */

int fastboot_init(void)
{
	struct usbd_context *uds_ctx = &fastboot_usbd;
	int err;

	printk("fastboot: init start\n");

	err = usbd_add_descriptor(uds_ctx, &fastboot_lang);
	if (err) { printk("fastboot: lang fail %d\n", err); return err; }

	err = usbd_add_descriptor(uds_ctx, &fastboot_mfr);
	if (err) { printk("fastboot: mfr fail %d\n", err); return err; }

	err = usbd_add_descriptor(uds_ctx, &fastboot_product);
	if (err) { printk("fastboot: product fail %d\n", err); return err; }

	err = usbd_msg_register_cb(uds_ctx, fb_msg_cb);
	if (err) { printk("fastboot: msg cb fail %d\n", err); return err; }

	// err = usbd_add_configuration(uds_ctx, USBD_SPEED_FS,
	// 			     &fastboot_fs_config);
	// if (err) { printk("fastboot: FS cfg fail %d\n", err); return err; }

	err = usbd_add_configuration(uds_ctx, USBD_SPEED_HS,
				     &fastboot_hs_config);
	if (err) { printk("fastboot: HS cfg fail %d\n", err); return err; }

	// err = usbd_register_class(uds_ctx, "fastboot_class",
	// 			  USBD_SPEED_FS, 1);
	// if (err) { printk("fastboot: FS reg fail %d\n", err); return err; }

	err = usbd_register_class(uds_ctx, "fastboot_class",
				  USBD_SPEED_HS, 1);
	if (err) { printk("fastboot: HS reg fail %d\n", err); return err; }

	err = usbd_init(uds_ctx);
	if (err) { printk("fastboot: init fail %d\n", err); return err; }

	if (usbd_can_detect_vbus(uds_ctx)) {
		printk("fastboot: VBUS detect enabled, waiting for cable\n");
	} else {
		printk("fastboot: no VBUS detect, enabling directly\n");
		err = usbd_enable(uds_ctx);
		if (err) { printk("fastboot: enable fail %d\n", err); return err; }
	}

	printk("fastboot: USB ready\n");
	return 0;
}

void fastboot_poll(void)
{
	/* USB is interrupt-driven, nothing to poll */
}
