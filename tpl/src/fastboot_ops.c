/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file fastboot_ops.c
 * @brief Fastboot Ops and partition registration (SYS_INIT)
 *
 * Runs before usbd_fastbootd init to register partitions and
 * flash operations before USB is initialized.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/usb/class/usbd_fastboot.h>

#include "flash_helper.h"
#include "firmware_header.h"

/* ============================================================
 * Partition Table (from DTS)
 * ============================================================ */

static const struct fastboot_part fb_parts[] = {
	FASTBOOT_PART_DEFINE(DT_NODELABEL(boot_partition),  FW_MAGIC_SPL, true),
	FASTBOOT_PART_DEFINE(DT_NODELABEL(slot0_partition), FW_MAGIC_TPL, false),
	FASTBOOT_PART_DEFINE(DT_NODELABEL(slot1_partition), FW_MAGIC_APP, false),
};

/* ============================================================
 * Ops
 * ============================================================ */

static int fb_flash_erase(uint32_t addr, uint32_t size)
{
	if (addr >= 0x08000000 && addr < 0x08100000) {
		return flash_internal_erase(addr, size);
	}
	return flash_external_erase(addr, size);
}

static int fb_flash_write(uint32_t addr, const uint8_t *data, uint32_t size)
{
	if (addr >= 0x08000000 && addr < 0x08100000) {
		return flash_internal_write(addr, data, size);
	}
	return flash_external_write(addr, data, size);
}

static int fb_firmware_verify(const uint8_t *data, uint32_t size,
			      uint32_t expected_magic)
{
	if (size < sizeof(struct firmware_header)) {
		return -EINVAL;
	}
	const struct firmware_header *hdr =
		(const struct firmware_header *)data;

	return (hdr->magic == expected_magic) ? 0 : -EINVAL;
}

static struct fastboot_ops fb_ops = {
	.flash_erase     = fb_flash_erase,
	.flash_write     = fb_flash_write,
	.firmware_verify = fb_firmware_verify,
};

/* ============================================================
 * SYS_INIT: runs before usbd_fastbootd
 * ============================================================ */

static int fastboot_ops_init(void)
{
	fastboot_register_partitions(fb_parts, ARRAY_SIZE(fb_parts));
	fastboot_register_ops(&fb_ops);
	return 0;
}

SYS_INIT(fastboot_ops_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
