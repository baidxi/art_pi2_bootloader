/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FIRMWARE_LOADER_H
#define FIRMWARE_LOADER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "firmware_header.h"

#define FW_LOADER_SUCCESS           0
#define FW_LOADER_ERR_MAGIC         -1
#define FW_LOADER_ERR_HEADER_VER    -2
#define FW_LOADER_ERR_HEADER_CRC    -3
#define FW_LOADER_ERR_EXEC_MODE     -4
#define FW_LOADER_ERR_LOAD_ADDR     -5
#define FW_LOADER_ERR_CHECKSUM      -6
#define FW_LOADER_ERR_RAM_TYPE      -7
#define FW_LOADER_ERR_JUMP          -8


int firmware_header_verify(const struct firmware_header *hdr,
                           uint32_t expected_magic);
int firmware_verify_checksum(const struct firmware_header *hdr,
                             const uint8_t *data);
int firmware_load_and_jump(const struct firmware_header *hdr,
                           uint32_t flash_base,
                           int (*flash_read_fn)(uint32_t addr,
                                                uint8_t *data,
                                                size_t len));
int firmware_verify_from_flash(uint32_t flash_addr,
                               struct firmware_header *hdr,
                               uint32_t expected_magic,
                               int (*flash_read_fn)(uint32_t addr,
                                                    uint8_t *data,
                                                    size_t len));

#endif /* FIRMWARE_LOADER_H */
