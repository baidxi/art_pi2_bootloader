/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "flash_helper.h"
#include <zephyr/sys/printk.h>
#include <string.h>

/* Flash 设备指针 */
static const struct device *flash_internal_dev = NULL;
static const struct device *flash_external_dev = NULL;

/* CRC16 表 (多项式: 0x1021) */
static const uint16_t crc16_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

int flash_internal_init(void)
{
    flash_internal_dev = DEVICE_DT_GET(DT_NODELABEL(flash));

    if (!device_is_ready(flash_internal_dev)) {
        printk("Error: Internal flash device not ready\n");
        return FLASH_OP_ERROR;
    }

    return FLASH_OP_SUCCESS;
}

int flash_external_init(void)
{
    flash_external_dev = DEVICE_DT_GET(DT_NODELABEL(ext_flash_ctrl));

    if (!device_is_ready(flash_external_dev)) {
        printk("Error: External flash device not ready\n");
        return FLASH_OP_ERROR;
    }

    return FLASH_OP_SUCCESS;
}

int flash_internal_erase(uint32_t addr, size_t len)
{
    struct flash_pages_info page_info;
    off_t offset;
    int rc;

    if (flash_internal_dev == NULL) {
        if (flash_internal_init() != FLASH_OP_SUCCESS) {
            return FLASH_OP_ERROR;
        }
    }

    if (addr < FLASH_BASE_ADDR || (addr + len) > (FLASH_BASE_ADDR + SPL_FLASH_SIZE)) {
        printk("Error: Invalid internal flash address range\n");
        return FLASH_OP_ERROR;
    }

    offset = addr - FLASH_BASE_ADDR;

    rc = flash_get_page_info_by_offs(flash_internal_dev, offset, &page_info);
    if (rc != 0) {
        printk("Error: Failed to get page info\n");
        return FLASH_OP_ERROR;
    }

    rc = flash_erase(flash_internal_dev, page_info.start_offset, page_info.size);
    if (rc != 0) {
        printk("Error: Flash erase failed (rc=%d)\n", rc);
        return FLASH_OP_ERROR;
    }

    return FLASH_OP_SUCCESS;
}

int flash_internal_write(uint32_t addr, const uint8_t *data, size_t len)
{
    off_t offset;
    int rc;

    if (flash_internal_dev == NULL) {
        if (flash_internal_init() != FLASH_OP_SUCCESS) {
            return FLASH_OP_ERROR;
        }
    }

    if (addr < FLASH_BASE_ADDR || (addr + len) > (FLASH_BASE_ADDR + SPL_FLASH_SIZE)) {
        printk("Error: Invalid internal flash address range\n");
        return FLASH_OP_ERROR;
    }

    if ((addr & 0x3) != 0) {
        printk("Error: Flash write address must be 4-byte aligned\n");
        return FLASH_OP_ERROR;
    }

    offset = addr - FLASH_BASE_ADDR;

    rc = flash_write(flash_internal_dev, offset, data, len);
    if (rc != 0) {
        printk("Error: Flash write failed (rc=%d)\n", rc);
        return FLASH_OP_ERROR;
    }

    return FLASH_OP_SUCCESS;
}

int flash_internal_read(uint32_t addr, uint8_t *data, size_t len)
{
    off_t offset;
    int rc;

    if (flash_internal_dev == NULL) {
        if (flash_internal_init() != FLASH_OP_SUCCESS) {
            return FLASH_OP_ERROR;
        }
    }

    if (addr < FLASH_BASE_ADDR || (addr + len) > (FLASH_BASE_ADDR + SPL_FLASH_SIZE)) {
        printk("Error: Invalid internal flash address range\n");
        return FLASH_OP_ERROR;
    }

    offset = addr - FLASH_BASE_ADDR;

    rc = flash_read(flash_internal_dev, offset, data, len);
    if (rc != 0) {
        printk("Error: Flash read failed (rc=%d)\n", rc);
        return FLASH_OP_ERROR;
    }

    return FLASH_OP_SUCCESS;
}

int flash_external_erase(uint32_t addr, size_t len)
{
    struct flash_pages_info page_info;
    off_t offset;
    int rc;

    if (flash_external_dev == NULL) {
        if (flash_external_init() != FLASH_OP_SUCCESS) {
            return FLASH_OP_ERROR;
        }
    }

    offset = addr - EXT_FLASH_BASE_ADDR;

    rc = flash_get_page_info_by_offs(flash_external_dev, offset, &page_info);
    if (rc != 0) {
        printk("Error: Failed to get page info\n");
        return FLASH_OP_ERROR;
    }

    rc = flash_erase(flash_external_dev, page_info.start_offset, page_info.size);
    if (rc != 0) {
        printk("Error: External flash erase failed (rc=%d)\n", rc);
        return FLASH_OP_ERROR;
    }

    return FLASH_OP_SUCCESS;
}

int flash_external_write(uint32_t addr, const uint8_t *data, size_t len)
{
    off_t offset;
    int rc;

    if (flash_external_dev == NULL) {
        if (flash_external_init() != FLASH_OP_SUCCESS) {
            return FLASH_OP_ERROR;
        }
    }

    if ((addr & 0x3) != 0) {
        printk("Error: Flash write address must be 4-byte aligned\n");
        return FLASH_OP_ERROR;
    }

    offset = addr - EXT_FLASH_BASE_ADDR;

    rc = flash_write(flash_external_dev, offset, data, len);
    if (rc != 0) {
        printk("Error: External flash write failed (rc=%d)\n", rc);
        return FLASH_OP_ERROR;
    }

    return FLASH_OP_SUCCESS;
}

int flash_external_read(uint32_t addr, uint8_t *data, size_t len)
{
    off_t offset;
    int rc;

    if (flash_external_dev == NULL) {
        if (flash_external_init() != FLASH_OP_SUCCESS) {
            return FLASH_OP_ERROR;
        }
    }

    offset = addr - EXT_FLASH_BASE_ADDR;

    rc = flash_read(flash_external_dev, offset, data, len);
    if (rc != 0) {
        printk("Error: External flash read failed (rc=%d)\n", rc);
        return FLASH_OP_ERROR;
    }

    return FLASH_OP_SUCCESS;
}

bool flash_verify_magic(uint32_t addr, uint32_t magic)
{
    uint32_t *ptr = (uint32_t *)addr;

    if ((addr & 0x3) != 0) {
        return false;
    }

    return (*ptr == magic);
}

uint16_t flash_calc_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;

    while (len--) {
        crc = (crc << 8) ^ crc16_table[((crc >> 8) ^ *data++) & 0xFF];
    }

    return crc;
}

bool flash_verify_crc(uint32_t addr, size_t len, uint16_t expected_crc)
{
    uint16_t calc_crc;
    const uint8_t *data = (const uint8_t *)addr;

    calc_crc = flash_calc_crc16(data, len);

    return (calc_crc == expected_crc);
}

int flash_copy_ext_to_int(uint32_t dst_addr, uint32_t src_addr, size_t len)
{
    uint8_t *src_ptr;
    int rc;

    /* 源地址在外部 Flash Memory Map 区域，可以直接读取 */
    src_ptr = (uint8_t *)src_addr;

    /* 按页写入内部 Flash */
    size_t offset = 0;
    while (offset < len) {
        size_t write_len = (len - offset > 256) ? 256 : (len - offset);

        rc = flash_internal_write(dst_addr + offset, src_ptr + offset, write_len);
        if (rc != 0) {
            printk("Error: Copy ext to int failed at offset 0x%x\n", offset);
            return FLASH_OP_ERROR;
        }

        offset += write_len;
    }

    return FLASH_OP_SUCCESS;
}

int flash_copy_ext_to_ext(uint32_t dst_addr, uint32_t src_addr, size_t len)
{
    uint8_t *src_ptr;
    int rc;

    /* 源地址在外部 Flash Memory Map 区域，可以直接读取 */
    src_ptr = (uint8_t *)src_addr;

    /* 按页写入外部 Flash */
    size_t offset = 0;
    while (offset < len) {
        size_t write_len = (len - offset > 256) ? 256 : (len - offset);

        rc = flash_external_write(dst_addr + offset, src_ptr + offset, write_len);
        if (rc != 0) {
            printk("Error: Copy ext to ext failed at offset 0x%x\n", offset);
            return FLASH_OP_ERROR;
        }

        offset += write_len;
    }

    return FLASH_OP_SUCCESS;
}

int flash_storage_write(uint32_t offset, const uint8_t *data, size_t len)
{
    uint32_t addr = EXT_FLASH_STORAGE_ADDR + offset;

    if (offset + len > EXT_FLASH_STORAGE_SIZE) {
        printk("Error: Storage write out of bounds\n");
        return FLASH_OP_ERROR;
    }

    return flash_external_write(addr, data, len);
}

int flash_storage_read(uint32_t offset, uint8_t *data, size_t len)
{
    uint32_t addr = EXT_FLASH_STORAGE_ADDR + offset;

    if (offset + len > EXT_FLASH_STORAGE_SIZE) {
        printk("Error: Storage read out of bounds\n");
        return FLASH_OP_ERROR;
    }

    return flash_external_read(addr, data, len);
}

int flash_set_update_request(uint32_t req)
{
    return flash_storage_write(STORAGE_OFFSET_UPDATE_REQ,
                               (const uint8_t *)&req, sizeof(req));
}

uint32_t flash_get_update_request(void)
{
    uint32_t req = UPDATE_REQ_NONE;

    flash_storage_read(STORAGE_OFFSET_UPDATE_REQ,
                       (uint8_t *)&req, sizeof(req));

    return req;
}

int flash_clear_update_request(void)
{
    uint32_t none = UPDATE_REQ_NONE;
    return flash_storage_write(STORAGE_OFFSET_UPDATE_REQ,
                               (const uint8_t *)&none, sizeof(none));
}
