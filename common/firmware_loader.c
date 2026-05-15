/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/crc.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include "firmware_loader.h"

LOG_MODULE_DECLARE(loader, CONFIG_LOG_DEFAULT_LEVEL);

static bool is_load_addr_valid(uint32_t exec_mode, uint32_t load_addr)
{
    if (exec_mode == FW_EXEC_XIP) {
        return (load_addr >= 0x70000000 && load_addr <= 0x702FFFFF);
    } else if (exec_mode == FW_EXEC_RAM) {
        return (load_addr >= 0x20000000 && load_addr <= 0x20050000) ||
               (load_addr >= 0x24000000 && load_addr <= 0x24080000);
    }
    return false;
}

int firmware_header_verify(const struct firmware_header *hdr,
                           uint32_t expected_magic)
{
    uint32_t calc_crc;
    const uint8_t *header_bytes;

    if (hdr == NULL) {
        LOG_ERR("FW_HEADER: NULL pointer");
        return FW_LOADER_ERR_MAGIC;
    }

    if (hdr->magic != expected_magic) {
        LOG_ERR("FW_HEADER: Magic mismatch: expected 0x%08X, got 0x%08X",
               expected_magic, hdr->magic);
        return FW_LOADER_ERR_MAGIC;
    }

    if (hdr->header_version != 1) {
        LOG_ERR("FW_HEADER: Unsupported header version: %u",
               hdr->header_version);
        return FW_LOADER_ERR_HEADER_VER;
    }

    header_bytes = (const uint8_t *)hdr;
    calc_crc = crc32_ieee(header_bytes, HEADER_CRC_REGION_SIZE);
    if (calc_crc != hdr->header_crc) {
        LOG_ERR("FW_HEADER: Header CRC mismatch: calc 0x%08X, stored 0x%08X",
               calc_crc, hdr->header_crc);
        return FW_LOADER_ERR_HEADER_CRC;
    }

    if (hdr->exec_mode != FW_EXEC_XIP && hdr->exec_mode != FW_EXEC_RAM) {
        LOG_ERR("FW_HEADER: Invalid exec_mode: 0x%08X", hdr->exec_mode);
        return FW_LOADER_ERR_EXEC_MODE;
    }

    if (!is_load_addr_valid(hdr->exec_mode, hdr->load_addr)) {
        LOG_ERR("FW_HEADER: Invalid load_addr: 0x%08X for mode 0x%08X",
               hdr->load_addr, hdr->exec_mode);
        return FW_LOADER_ERR_LOAD_ADDR;
    }

    LOG_DBG("FW_HEADER: Verification OK (magic=0x%08X, ver=%u, size=%u, "
           "mode=0x%08X, load=0x%08X)",
           hdr->magic, hdr->header_version, hdr->firmware_size,
           hdr->exec_mode, hdr->load_addr);

    return FW_LOADER_SUCCESS;
}

int firmware_verify_checksum(const struct firmware_header *hdr,
                             const uint8_t *data)
{
    uint32_t calc_crc;
    uint32_t payload_size;

    if (hdr == NULL || data == NULL) {
        LOG_ERR("NULL pointer");
        return FW_LOADER_ERR_CHECKSUM;
    }

    if (hdr->firmware_size <= FIRMWARE_HEADER_SIZE) {
        LOG_ERR("Invalid firmware_size: %u", hdr->firmware_size);
        return FW_LOADER_ERR_CHECKSUM;
    }

    payload_size = hdr->firmware_size - FIRMWARE_HEADER_SIZE;

    calc_crc = crc32_ieee(data, payload_size);

    if (calc_crc != hdr->checksum) {
        LOG_ERR("Mismatch: calc 0x%08X, stored 0x%08X",
               calc_crc, hdr->checksum);
        return FW_LOADER_ERR_CHECKSUM;
    }

    LOG_DBG("FW_CHECKSUM: OK (0x%08X)", calc_crc);
    return FW_LOADER_SUCCESS;
}

int firmware_load_and_jump(const struct firmware_header *hdr,
                           uint32_t flash_base,
                           int (*flash_read_fn)(uint32_t addr,
                                                uint8_t *data,
                                                size_t len))
{
    uint32_t vector_addr;
    uint32_t vector_data[2];
    uint32_t stack_ptr;
    uint32_t reset_handler;
    uint32_t vector_offset;
    void (*jump_to_app)(void);
    int rc;

    if (hdr == NULL) {
        LOG_ERR("NULL header pointer");
        return FW_LOADER_ERR_JUMP;
    }

    LOG_DBG("Loading firmware (mode=0x%08X, load_addr=0x%08X)",
           hdr->exec_mode, hdr->load_addr);

    if (hdr->vector_offset != 0 && hdr->vector_offset != 0xFFFFFFFF) {
        vector_offset = hdr->vector_offset;
    } else {
        vector_offset = FIRMWARE_HEADER_SIZE;
        LOG_WRN("vector_offset not set, using default %u",
               vector_offset);
    }

    if (hdr->exec_mode == FW_EXEC_XIP) {
        vector_addr = flash_base + vector_offset;
        LOG_INF("XIP mode, vector table at 0x%08X", vector_addr);

        /*
         * 通过 flash_read_fn 回调读取向量表前 2 个字（栈指针和复位向量）。
         *
         * 为什么不直接解引用 Flash 地址？
         * 对于 XSPI 外部 Flash，flash_read() API 使用间接模式（SPI 命令）
         * 读取数据，这会导致 XSPI 控制器退出 Memory Map 模式。
         * 因此，在调用 firmware_verify_from_flash()（内部调用 flash_read）
         * 之后，Memory Map 模式可能已失效，直接解引用 Flash 地址会读到
         * 无效数据（全 0 或全 F）。
         *
         * 使用 flash_read_fn 回调可以确保通过间接模式正确读取数据。
         */
        if (flash_read_fn != NULL) {
            rc = flash_read_fn(vector_addr, (uint8_t *)vector_data,
                               sizeof(vector_data));
            if (rc != 0) {
                LOG_ERR("Failed to read vector table from 0x%08X "
                       "(rc=%d)", vector_addr, rc);
                return FW_LOADER_ERR_JUMP;
            }
            stack_ptr = vector_data[0];
            reset_handler = vector_data[1];
        } else {
            uint32_t *vector_table = (uint32_t *)vector_addr;
            stack_ptr = vector_table[0];
            reset_handler = vector_table[1];
        }
    } else if (hdr->exec_mode == FW_EXEC_RAM) {
        uint32_t copy_size;
        uint32_t src_addr;
        uint32_t dst_addr;

        if (hdr->ram_type == FW_RAM_TYPE_EXTERNAL) {
            /* 外部 RAM 可能需要初始化 */
            if (hdr->ram_init_offset != 0 && hdr->ram_init_size > 0) {
                /* TODO: 执行 RAM 初始化代码
                 * 从 flash_base + hdr->ram_init_offset 复制 ram_init_size 字节
                 * 到 hdr->ram_init_load_addr，然后跳转到 hdr->ram_init_entry 执行
                 */
                LOG_DBG("RAM init code at offset %u, size %u",
                       hdr->ram_init_offset, hdr->ram_init_size);
                LOG_WRN("RAM init not yet implemented");
            }
        }

        src_addr = flash_base;
        dst_addr = hdr->load_addr;
        copy_size = hdr->firmware_size;

        LOG_DBG("FW_LOAD: Copying %u bytes from 0x%08X to 0x%08X\n",
               copy_size, src_addr, dst_addr);

        /* 注意：这里需要从 Flash 读取数据并写入 RAM
         * 由于 Flash 是内存映射的，可以直接 memcpy
         * 但如果 Flash 不是内存映射的，需要使用 flash_read API
         */
        if (flash_read_fn != NULL) {
            /* 通过回调逐块读取 Flash 数据到 RAM */
            uint32_t offset = 0;
            const uint32_t chunk_size = 256;

            while (offset < copy_size) {
                uint32_t chunk = (copy_size - offset > chunk_size) ?
                                 chunk_size : (copy_size - offset);
                rc = flash_read_fn(src_addr + offset,
                                   (uint8_t *)dst_addr + offset, chunk);
                if (rc != 0) {
                    LOG_ERR("Flash read failed at offset 0x%X "
                           "(rc=%d)", offset, rc);
                    return FW_LOADER_ERR_JUMP;
                }
                offset += chunk;
            }
        } else {
            memcpy((void *)dst_addr, (const void *)src_addr, copy_size);
        }

        vector_addr = dst_addr + vector_offset;
        LOG_DBG("RAM mode, vector table at 0x%08X", vector_addr);

        {
            uint32_t *vector_table = (uint32_t *)vector_addr;
            stack_ptr = vector_table[0];
            reset_handler = vector_table[1];
        }
    } else {
        LOG_ERR("Unsupported exec_mode: 0x%08X", hdr->exec_mode);
        return FW_LOADER_ERR_JUMP;
    }

    __disable_irq();

    SysTick->CTRL = 0;
    SysTick->VAL = 0;

    SCB->VTOR = vector_addr;

    if (!((stack_ptr >= 0x20000000 && stack_ptr <= 0x20050000) ||
          (stack_ptr >= 0x24000000 && stack_ptr <= 0x24080000))) {
        LOG_ERR("Invalid stack pointer: 0x%08X\n", stack_ptr);
        return FW_LOADER_ERR_JUMP;
    }

    if (reset_handler < vector_addr ||
        reset_handler > vector_addr + 0x100000) {
        LOG_ERR("Invalid reset handler: 0x%08X\n",
               reset_handler);
        return FW_LOADER_ERR_JUMP;
    }

    __set_MSP(stack_ptr);

    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
    }

    LOG_DBG("FW_LOAD: Jumping to 0x%08X (SP=0x%08X, Reset=0x%08X)",
           vector_addr, stack_ptr, reset_handler);

    jump_to_app = (void (*)(void))reset_handler;
    jump_to_app();

    return FW_LOADER_ERR_JUMP;
}

int firmware_verify_from_flash(uint32_t flash_addr,
                               struct firmware_header *hdr,
                               uint32_t expected_magic,
                               int (*flash_read_fn)(uint32_t addr,
                                                    uint8_t *data,
                                                    size_t len))
{
    int rc;

    if (hdr == NULL || flash_read_fn == NULL) {
        LOG_ERR("NULL pointer\n");
        return FW_LOADER_ERR_MAGIC;
    }

    rc = flash_read_fn(flash_addr, (uint8_t *)hdr, FIRMWARE_HEADER_SIZE);
    if (rc != 0) {
        LOG_ERR("Failed to read header from 0x%08X (rc=%d)",
               flash_addr, rc);
        return FW_LOADER_ERR_MAGIC;
    }

    return firmware_header_verify(hdr, expected_magic);
}
