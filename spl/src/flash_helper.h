/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FLASH_HELPER_H
#define FLASH_HELPER_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>

#define FLASH_BASE_ADDR            0x08000000
#define SPL_FLASH_SIZE             (64 * 1024)

#define EXT_FLASH_BASE_ADDR        0x70000000
#define EXT_FLASH_TPL_ADDR         0x70000000
#define EXT_FLASH_TPL_SIZE         (256 * 1024)
#define EXT_FLASH_SLOT0_ADDR       0x70040000
#define EXT_FLASH_SLOT0_SIZE       (1 * 1024 * 1024)
#define EXT_FLASH_SLOT1_ADDR       0x70140000
#define EXT_FLASH_SLOT1_SIZE       (1 * 1024 * 1024)

#define SPL_MAGIC                  0x53504C53
#define TPL_MAGIC                  0x54504C54
#define APP_MAGIC                  0x41505041

#define FLASH_OP_SUCCESS           0
#define FLASH_OP_ERROR             -1
#define FLASH_OP_VERIFY_ERROR      -2
#define FLASH_OP_TIMEOUT           -3

int flash_internal_init(void);
int flash_external_init(void);
int flash_internal_erase(uint32_t addr, size_t len);
int flash_internal_write(uint32_t addr, const uint8_t *data, size_t len);
int flash_internal_read(uint32_t addr, uint8_t *data, size_t len);
int flash_external_erase(uint32_t addr, size_t len);
int flash_external_write(uint32_t addr, const uint8_t *data, size_t len);
int flash_external_read(uint32_t addr, uint8_t *data, size_t len);
bool flash_verify_magic(uint32_t addr, uint32_t magic);
uint16_t flash_calc_crc16(const uint8_t *data, size_t len);
bool flash_verify_crc(uint32_t addr, size_t len, uint16_t expected_crc);

#endif /* FLASH_HELPER_H */
