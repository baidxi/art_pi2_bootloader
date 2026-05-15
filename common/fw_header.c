/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "firmware_header.h"

const struct firmware_header fw_header
__attribute__((section(".fw_header"))) = {
    .magic          = FW_MAGIC_VALUE,
    .header_version = 1,
    .firmware_size  = FW_SIZE_PLACEHOLDER,
    .exec_mode      = FW_EXEC_XIP,
    .load_addr      = FW_LOAD_ADDR,
    .version        = FW_VERSION_VALUE,
    .vector_offset  = FW_VECTOR_OFFSET_PLACEHOLDER,
    .reserved1      = {0},
    .ram_type           = FW_RAM_TYPE_NONE,
    .ram_init_offset    = 0,
    .ram_init_size      = 0,
    .ram_init_load_addr = 0,
    .ram_init_entry     = 0,
    .reserved2          = {0},
    .build_date     = __DATE__,
    .build_time     = __TIME__,
    .checksum       = FW_CHECKSUM_PLACEHOLDER,
    .reserved3      = {0},
    .header_crc     = FW_HEADER_CRC_PLACEHOLDER,
    .reserved4      = {0},
};
