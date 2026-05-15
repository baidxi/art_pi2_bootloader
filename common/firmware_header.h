/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FIRMWARE_HEADER_H
#define FIRMWARE_HEADER_H

#include <stdint.h>
#include <stdbool.h>


#define FW_MAGIC_SPL    0x53504C53
#define FW_MAGIC_TPL    0x54504C54
#define FW_MAGIC_APP    0x41505041
#define FW_EXEC_XIP     0x00000001
#define FW_EXEC_RAM     0x00000002
#define FW_RAM_TYPE_NONE     0x00000000
#define FW_RAM_TYPE_INTERNAL 0x00000001
#define FW_RAM_TYPE_EXTERNAL 0x00000002
#define FW_SIZE_PLACEHOLDER         0xFFFFFFFF
#define FW_CHECKSUM_PLACEHOLDER     0x00000000
#define FW_HEADER_CRC_PLACEHOLDER   0x00000000
#define FW_VECTOR_OFFSET_PLACEHOLDER 0xFFFFFFFF
#define FIRMWARE_HEADER_SIZE        128
#define HEADER_CRC_REGION_SIZE      0x60

struct __attribute__((packed)) firmware_header {
    uint32_t magic;
    uint32_t header_version;
    uint32_t firmware_size;
    uint32_t exec_mode;
    uint32_t load_addr;
    uint32_t version;
    uint32_t vector_offset;
    uint8_t reserved1[4];
    uint32_t ram_type;
    uint32_t ram_init_offset;
    uint32_t ram_init_size;
    uint32_t ram_init_load_addr;
    uint32_t ram_init_entry;
    uint8_t reserved2[12];
    char build_date[16];
    char build_time[8];
    uint32_t checksum;
    uint8_t reserved3[4];
    uint32_t header_crc;
    uint8_t reserved4[28];
};

_Static_assert(sizeof(struct firmware_header) == FIRMWARE_HEADER_SIZE,
               "struct firmware_header must be exactly 128 bytes");

#endif /* FIRMWARE_HEADER_H */
