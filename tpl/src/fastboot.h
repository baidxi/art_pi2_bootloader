/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FASTBOOT_H
#define FASTBOOT_H

/**
 * @file fastboot.h
 * @brief Fastboot protocol implementation for TPL bootloader
 *
 * Implements the Android Fastboot protocol for firmware updates via USB.
 * Uses Zephyr's new USB device stack (USB_DEVICE_STACK_NEXT).
 */

#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================
 * Fastboot USB Device Identification
 * ============================================================ */

#define FASTBOOT_VID             0x2FE3  /* Zephyr Project VID */
#define FASTBOOT_PID             0x1001  /* TPL Fastboot PID */

/* ============================================================
 * Fastboot USB Interface Descriptor Values
 * ============================================================ */

#define FASTBOOT_IFACE_CLASS      0xFF
#define FASTBOOT_IFACE_SUBCLASS   0x42
#define FASTBOOT_IFACE_PROTOCOL   0x03

/* ============================================================
 * USB Endpoint Assignments
 * ============================================================ */

#define FASTBOOT_BULK_OUT_EP      0x01   /* EP1 OUT (host to device) */
#define FASTBOOT_BULK_IN_EP       0x81   /* EP1 IN  (device to host) */

/* ============================================================
 * Protocol Constants
 * ============================================================ */

#define FASTBOOT_MAX_CMDBUF       64
#define FASTBOOT_MAX_RSPBUF       64

/*
 * Download buffer size: 32KB.
 * SRAM0 (zephyr,sram) = 456KB but BSS/data/text share this space.
 * TPL itself plus USB stack takes significant RAM.
 * Start small for testing; increase once USB enumeration is verified.
 */
#define FASTBOOT_DOWNLOAD_BUF     (32 * 1024)

/* Fastboot response prefixes */
#define FASTBOOT_RSP_OKAY         "OKAY"
#define FASTBOOT_RSP_FAIL         "FAIL"
#define FASTBOOT_RSP_DATA         "DATA"
#define FASTBOOT_RSP_INFO         "INFO"

/* ============================================================
 * Fastboot State Machine
 * ============================================================ */

enum fastboot_state {
	FASTBOOT_STATE_OFFLINE,      /* USB not yet enabled */
	FASTBOOT_STATE_IDLE,         /* Waiting for commands */
	FASTBOOT_STATE_DOWNLOAD,     /* Receiving firmware data */
	FASTBOOT_STATE_DOWNLOADED,   /* Firmware ready, waiting for flash/cmd */
	FASTBOOT_STATE_ERROR,        /* Error, need to recover */
};

/* ============================================================
 * Fastboot Context
 * ============================================================ */

struct fastboot_ctx {
	enum fastboot_state state;
	uint8_t *download_buf;
	uint32_t download_size;
	uint32_t download_received;
};

/* ============================================================
 * Public API
 * ============================================================ */

/**
 * @brief Initialize Fastboot USB device
 *
 * @return 0 on success, negative errno on failure
 */
int fastboot_init(void);

/**
 * @brief Fastboot poll function (for main loop)
 */
void fastboot_poll(void);

#endif /* FASTBOOT_H */
