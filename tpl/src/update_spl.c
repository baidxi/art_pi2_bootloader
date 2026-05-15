/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file update_spl.c
 * @brief SPL (Stage 1) 更新逻辑
 *
 * TPL 通过 YModem 协议接收新的 SPL 固件，
 * 擦写内部 Flash 的 SPL 区域（0x08000000），
 * 写入完成后验证固件头，然后重启。
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

/* YModem 相关声明（复用 SPL 的 ymodem.h） */
#include "ymodem.h"

/* ---- YModem 逐块写入回调 ---- */
static int spl_block_write(uint32_t addr, const uint8_t *data,
                           size_t len, uint32_t total,
                           int file_size, void *priv)
{
    (void)total;
    (void)file_size;
    (void)priv;
    return flash_internal_write(addr, data, len);
}

/**
 * @brief 更新 SPL 固件
 *
 * 流程：
 * 1. 提示用户通过 YModem 发送 SPL 固件
 * 2. 擦除内部 Flash 的 SPL 区域
 * 3. 接收 YModem 数据并逐块写入内部 Flash
 * 4. 验证固件头（魔术字、CRC 等）
 * 5. 重启
 *
 * @return 0 成功，负数失败
 */
int update_spl_start(void)
{
    struct ymodem_config ymodem_cfg;
    int rc;

    menu_printf("========================================\n");
    menu_printf("  SPL (Stage 1) Update\n");
    menu_printf("========================================\n");
    menu_printf("Target: Internal Flash @ 0x%08X\n", FLASH_BASE_ADDR);
    menu_printf("Size:   %d KB\n", SPL_FLASH_SIZE / 1024);
    menu_printf("\n");
    menu_printf("Please send SPL firmware via YModem...\n");
    menu_printf("(Use TeraTerm or lrzsz to send the file)\n");
    menu_printf("\n");

    /* 擦除内部 Flash */
    menu_printf("Erasing internal flash...\n");
    rc = flash_internal_erase(FLASH_BASE_ADDR, SPL_FLASH_SIZE);
    if (rc != 0) {
        menu_printf("ERROR: Failed to erase internal flash!\n");
        return -1;
    }
    menu_printf("Internal flash erased.\n");

    /* 配置 YModem - 逐块写入内部 Flash */
    ymodem_cfg.dev = menu_get_uart_dev();
    ymodem_cfg.flash_addr = FLASH_BASE_ADDR;
    ymodem_cfg.max_size = SPL_FLASH_SIZE;
    ymodem_cfg.block_write_cb = spl_block_write;
    ymodem_cfg.priv = NULL;

    if (!ymodem_cfg.dev) {
        menu_printf("ERROR: UART device not ready!\n");
        return -1;
    }

    /* 接收固件（逐块写入内部 Flash） */
    rc = ymodem_recv(&ymodem_cfg);
    if (rc < 0) {
        menu_printf("ERROR: YModem receive failed (%d)!\n", rc);
        return -1;
    }

    menu_printf("Received %d bytes\n", rc);

    /* 使用统一固件头验证代替旧的 Magic Number 检查 */
    {
        struct firmware_header hdr;
        int vrc;

        vrc = firmware_verify_from_flash(FLASH_BASE_ADDR, &hdr,
                                         FW_MAGIC_SPL, flash_internal_read);
        if (vrc == FW_LOADER_SUCCESS) {
            menu_printf("SPL firmware header verified OK\n");

            /* 验证有效代码的 CRC32 */
            {
                uint32_t payload_size = hdr.firmware_size - FIRMWARE_HEADER_SIZE;
                const uint8_t *payload = (const uint8_t *)(FLASH_BASE_ADDR
                                                            + FIRMWARE_HEADER_SIZE);

                if (payload_size > 0) {
                    vrc = firmware_verify_checksum(&hdr, payload);
                    if (vrc != FW_LOADER_SUCCESS) {
                        menu_printf("WARNING: SPL checksum verification failed (rc=%d)\n", vrc);
                        menu_printf("The firmware may be corrupted, but continuing...\n");
                    } else {
                        menu_printf("SPL checksum verified OK\n");
                    }
                }
            }
        } else {
            menu_printf("WARNING: SPL firmware header verification failed (rc=%d)!\n", vrc);
            menu_printf("The firmware may be invalid.\n");
            menu_printf("Rebooting anyway...\n");
        }
    }

    menu_printf("\nSPL update complete! Rebooting in 3 seconds...\n");
    k_sleep(K_SECONDS(3));
    sys_reboot(SYS_REBOOT_COLD);

    return 0;
}
