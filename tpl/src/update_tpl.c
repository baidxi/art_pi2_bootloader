/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file update_tpl.c
 * @brief TPL (Stage 2) 自更新逻辑
 *
 * TPL 通过 YModem 协议接收新的 TPL 固件，
 * 写入外部 Flash 的 TPL 分区（0x70000000），
 * 写入完成后验证固件头，然后重启。
 *
 * 注意：TPL 运行在外部 Flash 的 Memory Map 区域，
 * 更新自身时需要写入到 TPL 分区，但 TPL 代码正在该区域执行。
 * 解决方案：先将新固件写入 Slot1 暂存区，然后设置升级请求标志，
 * 重启后由 SPL 或 TPL 启动时检查升级请求，将 Slot1 复制到 TPL 分区。
 *
 * 固件验证：
 * - 使用 firmware_header_verify() 验证固件头（魔术字、CRC 等）
 * - 使用 firmware_verify_checksum() 验证有效代码 CRC32
 */

#include "flash_helper.h"
#include "menu.h"
#include "firmware_loader.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <string.h>

/* YModem 相关声明 */
#include "ymodem.h"

/* ---- YModem 逐块写入回调 ---- */
static int tpl_block_write(uint32_t addr, const uint8_t *data,
                           size_t len, uint32_t total,
                           int file_size, void *priv)
{
    (void)total;
    (void)file_size;
    (void)priv;
    return flash_external_write(addr, data, len);
}

/**
 * @brief 验证 Slot1 中的 TPL 固件（使用统一固件头验证）
 *
 * @return 0 成功，负数失败
 */
static int verify_slot1_tpl_firmware(void)
{
    struct firmware_header hdr;
    int rc;

    /* 从 Slot1 读取固件头 */
    rc = firmware_verify_from_flash(EXT_FLASH_SLOT1_ADDR, &hdr,
                                    FW_MAGIC_TPL, flash_external_read);
    if (rc != FW_LOADER_SUCCESS) {
        menu_printf("TPL firmware header verification failed (rc=%d)\n", rc);
        return rc;
    }

    /* 验证有效代码的 CRC32 */
    {
        uint32_t payload_size = hdr.firmware_size - FIRMWARE_HEADER_SIZE;
        const uint8_t *payload = (const uint8_t *)(EXT_FLASH_SLOT1_ADDR
                                                    + FIRMWARE_HEADER_SIZE);

        if (payload_size > 0) {
            rc = firmware_verify_checksum(&hdr, payload);
            if (rc != FW_LOADER_SUCCESS) {
                menu_printf("WARNING: TPL checksum verification failed (rc=%d)\n", rc);
                menu_printf("The firmware may be corrupted, but continuing...\n");
            } else {
                menu_printf("TPL checksum verified OK\n");
            }
        }
    }

    return FW_LOADER_SUCCESS;
}

/**
 * @brief 更新 TPL 固件（自更新）
 *
 * 流程：
 * 1. 提示用户通过 YModem 发送 TPL 固件
 * 2. 擦除 Slot1 区域（作为暂存区）
 * 3. 接收 YModem 数据并逐块写入 Slot1
 * 4. 验证固件头（魔术字、CRC 等）
 * 5. 设置 TPL 升级请求标志
 * 6. 重启，SPL 将检测到升级请求，将 Slot1 复制到 TPL 分区
 *
 * @return 0 成功，负数失败
 */
int update_tpl_start(void)
{
    struct ymodem_config ymodem_cfg;
    int rc;

    menu_printf("========================================\n");
    menu_printf("  TPL (Stage 2) Self-Update\n");
    menu_printf("========================================\n");
    menu_printf("Target: External Flash TPL @ 0x%08X\n", EXT_FLASH_TPL_ADDR);
    menu_printf("Size:   %d KB\n", EXT_FLASH_TPL_SIZE / 1024);
    menu_printf("\n");
    menu_printf("NOTE: New firmware will be written to Slot1 first,\n");
    menu_printf("then copied to TPL partition after reboot.\n");
    menu_printf("\n");
    menu_printf("Please send TPL firmware via YModem...\n");
    menu_printf("(Use TeraTerm or lrzsz to send the file)\n");
    menu_printf("\n");

    /* 擦除 Slot1 区域 */
    menu_printf("Erasing Slot1 (staging area)...\n");
    rc = flash_external_erase(EXT_FLASH_SLOT1_ADDR, EXT_FLASH_TPL_SIZE);
    if (rc != 0) {
        menu_printf("ERROR: Failed to erase Slot1!\n");
        return -1;
    }
    menu_printf("Slot1 erased.\n");

    /* 配置 YModem - 逐块写入 Slot1 暂存区 */
    ymodem_cfg.dev = menu_get_uart_dev();
    ymodem_cfg.flash_addr = EXT_FLASH_SLOT1_ADDR;
    ymodem_cfg.max_size = EXT_FLASH_TPL_SIZE;
    ymodem_cfg.block_write_cb = tpl_block_write;
    ymodem_cfg.priv = NULL;

    if (!ymodem_cfg.dev) {
        menu_printf("ERROR: UART device not ready!\n");
        return -1;
    }

    /* 接收固件到 Slot1（逐块写入） */
    rc = ymodem_recv(&ymodem_cfg);
    if (rc < 0) {
        menu_printf("ERROR: YModem receive failed (%d)!\n", rc);
        return -1;
    }

    menu_printf("Received %d bytes\n", rc);

    /* 使用统一固件头验证代替旧的 Magic Number 检查 */
    rc = verify_slot1_tpl_firmware();
    if (rc != FW_LOADER_SUCCESS) {
        menu_printf("WARNING: TPL firmware header verification failed!\n");
        menu_printf("The firmware may be invalid.\n");
        menu_printf("Aborting update...\n");
        return -1;
    }

    /* 设置 TPL 升级请求标志 */
    menu_printf("Setting TPL update request flag...\n");
    rc = flash_set_update_request(UPDATE_REQ_TPL);
    if (rc != 0) {
        menu_printf("ERROR: Failed to set update request!\n");
        return -1;
    }

    menu_printf("\nTPL update complete! Rebooting in 3 seconds...\n");
    menu_printf("SPL will copy new TPL from Slot1 to TPL partition.\n");
    k_sleep(K_SECONDS(3));
    sys_reboot(SYS_REBOOT_COLD);

    return 0;
}

/**
 * @brief 处理 TPL 升级请求（由 main.c 启动时调用）
 *
 * 检查是否有 TPL 升级请求，如果有：
 * 1. 擦除 TPL 分区
 * 2. 将 Slot1 中的新固件复制到 TPL 分区
 * 3. 验证固件头
 * 4. 清除升级请求标志
 *
 * @return 0 成功，负数失败（无升级请求也返回 0）
 */
int update_tpl_process_request(void)
{
    uint32_t req;
    int rc;

    req = flash_get_update_request();

    if (req != UPDATE_REQ_TPL) {
        return 0;  /* 无 TPL 升级请求 */
    }

    menu_printf("TPL update request detected!\n");
    menu_printf("Copying new TPL from Slot1 to TPL partition...\n");

    /* 验证 Slot1 中的 TPL 固件（使用统一固件头验证） */
    rc = verify_slot1_tpl_firmware();
    if (rc != FW_LOADER_SUCCESS) {
        menu_printf("ERROR: Invalid TPL firmware in Slot1!\n");
        flash_clear_update_request();
        return -1;
    }

    /* 擦除 TPL 分区 */
    menu_printf("Erasing TPL partition...\n");
    rc = flash_external_erase(EXT_FLASH_TPL_ADDR, EXT_FLASH_TPL_SIZE);
    if (rc != 0) {
        menu_printf("ERROR: Failed to erase TPL partition!\n");
        return -1;
    }

    /* 从 Slot1 复制到 TPL 分区 */
    menu_printf("Copying firmware...\n");
    rc = flash_copy_ext_to_ext(EXT_FLASH_TPL_ADDR, EXT_FLASH_SLOT1_ADDR,
                                EXT_FLASH_TPL_SIZE);
    if (rc != 0) {
        menu_printf("ERROR: Failed to copy firmware!\n");
        return -1;
    }

    /* 验证 TPL 分区（使用统一固件头验证） */
    {
        struct firmware_header hdr;

        rc = firmware_verify_from_flash(EXT_FLASH_TPL_ADDR, &hdr,
                                        FW_MAGIC_TPL, flash_external_read);
        if (rc == FW_LOADER_SUCCESS) {
            menu_printf("TPL update verified OK\n");
        } else {
            menu_printf("ERROR: TPL verification failed after copy (rc=%d)!\n", rc);
            return -1;
        }
    }

    /* 清除升级请求 */
    flash_clear_update_request();

    menu_printf("TPL update successful!\n");
    return 0;
}
