/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file update_app.c
 * @brief 应用 (Stage 3) 更新和启动逻辑
 *
 * 功能：
 * 1. 更新应用：通过 YModem 接收新应用固件，写入 Slot1
 * 2. 处理应用升级请求：将 Slot1 复制到 Slot0
 * 3. 启动应用：验证 Slot0 固件头，跳转到应用入口
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
#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/types.h>
#include <string.h>

/* YModem 相关声明 */
#include "ymodem.h"

/* ---- YModem 逐块写入回调 ---- */
static int app_block_write(uint32_t addr, const uint8_t *data,
                           size_t len, uint32_t total,
                           int file_size, void *priv)
{
    (void)total;
    (void)file_size;
    (void)priv;
    return flash_external_write(addr, data, len);
}

/**
 * @brief 验证 Slot1 中的固件（使用统一固件头验证）
 *
 * 从 Slot1 读取固件头，验证魔术字、CRC 等字段。
 *
 * @param expected_magic 期望的魔术字
 * @return 0 成功，负数失败
 */
static int verify_slot1_firmware(uint32_t expected_magic)
{
    struct firmware_header hdr;
    int rc;

    /* 从 Slot1 读取固件头 */
    rc = firmware_verify_from_flash(EXT_FLASH_SLOT1_ADDR, &hdr,
                                    expected_magic, flash_external_read);
    if (rc != FW_LOADER_SUCCESS) {
        menu_printf("Firmware header verification failed (rc=%d)\n", rc);
        return rc;
    }

    /* 验证有效代码的 CRC32
     * 由于固件在外部 Flash 中且是内存映射的，可以直接读取
     */
    {
        uint32_t payload_size = hdr.firmware_size - FIRMWARE_HEADER_SIZE;
        const uint8_t *payload = (const uint8_t *)(EXT_FLASH_SLOT1_ADDR
                                                    + FIRMWARE_HEADER_SIZE);

        if (payload_size > 0) {
            rc = firmware_verify_checksum(&hdr, payload);
            if (rc != FW_LOADER_SUCCESS) {
                menu_printf("WARNING: Firmware checksum verification failed (rc=%d)\n", rc);
                menu_printf("The firmware may be corrupted, but continuing...\n");
            } else {
                menu_printf("Firmware checksum verified OK\n");
            }
        }
    }

    return FW_LOADER_SUCCESS;
}

/**
 * @brief 更新应用固件
 *
 * 流程：
 * 1. 提示用户通过 YModem 发送应用固件
 * 2. 擦除 Slot1 区域
 * 3. 接收 YModem 数据并逐块写入 Slot1
 * 4. 验证固件头（魔术字、CRC 等）
 * 5. 设置应用升级请求标志
 * 6. 重启
 *
 * @return 0 成功，负数失败
 */
int update_app_start(void)
{
    struct ymodem_config ymodem_cfg;
    int rc;

    menu_printf("========================================\n");
    menu_printf("  Application (Stage 3) Update\n");
    menu_printf("========================================\n");
    menu_printf("Target: Slot1 @ 0x%08X\n", EXT_FLASH_SLOT1_ADDR);
    menu_printf("Size:   %d MB\n", EXT_FLASH_SLOT1_SIZE / (1024 * 1024));
    menu_printf("\n");
    menu_printf("Please send application firmware via YModem...\n");
    menu_printf("(Use TeraTerm or lrzsz to send the file)\n");
    menu_printf("\n");

    /* 擦除 Slot1 */
    menu_printf("Erasing Slot1...\n");
    rc = flash_external_erase(EXT_FLASH_SLOT1_ADDR, EXT_FLASH_SLOT1_SIZE);
    if (rc != 0) {
        menu_printf("ERROR: Failed to erase Slot1!\n");
        return -1;
    }
    menu_printf("Slot1 erased.\n");

    /* 配置 YModem - 逐块写入 Slot1 */
    ymodem_cfg.dev = menu_get_uart_dev();
    ymodem_cfg.flash_addr = EXT_FLASH_SLOT1_ADDR;
    ymodem_cfg.max_size = EXT_FLASH_SLOT1_SIZE;
    ymodem_cfg.block_write_cb = app_block_write;
    ymodem_cfg.priv = NULL;

    if (!ymodem_cfg.dev) {
        menu_printf("ERROR: UART device not ready!\n");
        return -1;
    }

    /* 接收固件（逐块写入 Slot1） */
    rc = ymodem_recv(&ymodem_cfg);
    if (rc < 0) {
        menu_printf("ERROR: YModem receive failed (%d)!\n", rc);
        return -1;
    }

    menu_printf("Received %d bytes\n", rc);

    /* 使用统一固件头验证代替旧的 Magic Number 检查 */
    rc = verify_slot1_firmware(FW_MAGIC_APP);
    if (rc != FW_LOADER_SUCCESS) {
        menu_printf("WARNING: Firmware header verification failed!\n");
        menu_printf("The firmware may be invalid.\n");
        menu_printf("Aborting update...\n");
        return -1;
    }

    /* 设置应用升级请求标志 */
    menu_printf("Setting application update request flag...\n");
    rc = flash_set_update_request(UPDATE_REQ_APP);
    if (rc != 0) {
        menu_printf("ERROR: Failed to set update request!\n");
        return -1;
    }

    menu_printf("\nApplication update complete! Rebooting in 3 seconds...\n");
    menu_printf("TPL will copy new app from Slot1 to Slot0 after reboot.\n");
    k_sleep(K_SECONDS(3));
    sys_reboot(SYS_REBOOT_COLD);

    return 0;
}

/**
 * @brief 处理应用升级请求（由 main.c 启动时调用）
 *
 * 检查是否有应用升级请求，如果有：
 * 1. 擦除 Slot0
 * 2. 将 Slot1 中的新固件复制到 Slot0
 * 3. 验证固件头
 * 4. 清除升级请求标志
 *
 * @return 0 成功，负数失败（无升级请求也返回 0）
 */
int update_app_process_request(void)
{
    uint32_t req;
    int rc;

    req = flash_get_update_request();

    if (req != UPDATE_REQ_APP) {
        return 0;  /* 无应用升级请求 */
    }

    menu_printf("Application update request detected!\n");
    menu_printf("Copying new app from Slot1 to Slot0...\n");

    /* 验证 Slot1 中的应用固件（使用统一固件头验证） */
    rc = verify_slot1_firmware(FW_MAGIC_APP);
    if (rc != FW_LOADER_SUCCESS) {
        menu_printf("ERROR: Invalid application firmware in Slot1!\n");
        flash_clear_update_request();
        return -1;
    }

    /* 擦除 Slot0 */
    menu_printf("Erasing Slot0...\n");
    rc = flash_external_erase(EXT_FLASH_SLOT0_ADDR, EXT_FLASH_SLOT0_SIZE);
    if (rc != 0) {
        menu_printf("ERROR: Failed to erase Slot0!\n");
        return -1;
    }

    /* 从 Slot1 复制到 Slot0 */
    menu_printf("Copying firmware...\n");
    rc = flash_copy_ext_to_ext(EXT_FLASH_SLOT0_ADDR, EXT_FLASH_SLOT1_ADDR,
                                EXT_FLASH_SLOT0_SIZE);
    if (rc != 0) {
        menu_printf("ERROR: Failed to copy firmware!\n");
        return -1;
    }

    /* 验证 Slot0（使用统一固件头验证） */
    {
        struct firmware_header hdr;

        rc = firmware_verify_from_flash(EXT_FLASH_SLOT0_ADDR, &hdr,
                                        FW_MAGIC_APP, flash_external_read);
        if (rc == FW_LOADER_SUCCESS) {
            menu_printf("Application update verified OK\n");
        } else {
            menu_printf("ERROR: Application verification failed after copy (rc=%d)!\n", rc);
            return -1;
        }
    }

    /* 清除升级请求 */
    flash_clear_update_request();

    menu_printf("Application update successful!\n");
    return 0;
}

/**
 * @brief 跳转到应用入口
 *
 * @param addr 应用基地址（含固件头）
 */
static void jump_to_app(uint32_t addr)
{
    uint32_t *vector_table;
    uint32_t stack_ptr;
    uint32_t reset_handler;
    void (*jump_func)(void);
    uint32_t entry_addr;

    /* 应用入口在固件头之后（偏移 0x80） */
    entry_addr = addr + FIRMWARE_HEADER_SIZE;

    menu_printf("Jumping to application at 0x%08X (entry 0x%08X)...\n",
                addr, entry_addr);

    /* 关闭全局中断 */
    __disable_irq();

    /* 关闭 SysTick */
    SysTick->CTRL = 0;
    SysTick->VAL = 0;

    /* 设置 VTOR 为向量表地址（固件头之后） */
    SCB->VTOR = entry_addr;

    /* 获取栈指针和复位处理程序地址 */
    vector_table = (uint32_t *)entry_addr;
    stack_ptr = vector_table[0];
    reset_handler = vector_table[1];

    /* 检查栈指针是否在有效范围（内部 SRAM 或外部 PSRAM） */
    if (!((stack_ptr >= 0x20000000 && stack_ptr <= 0x20050000) ||
          (stack_ptr >= 0x24000000 && stack_ptr <= 0x24080000))) {
        menu_printf("ERROR: Invalid stack pointer: 0x%08X\n", stack_ptr);
        return;
    }

    /* 检查复位处理程序地址 */
    if (reset_handler < entry_addr || reset_handler > entry_addr + 0x100000) {
        menu_printf("ERROR: Invalid reset handler: 0x%08X\n", reset_handler);
        return;
    }

    /* 设置主栈指针 */
    __set_MSP(stack_ptr);

    /* 清除所有挂起的中断 */
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
    }

    /* 跳转到应用程序 */
    jump_func = (void (*)(void))reset_handler;
    jump_func();
}

/**
 * @brief 启动应用
 *
 * 验证 Slot0 中的应用固件，然后跳转。
 * 如果 Slot0 无效，提示用户。
 */
void boot_app(void)
{
    struct firmware_header hdr;
    int rc;

    menu_printf("Verifying application in Slot0...\n");

    /* 使用统一固件头验证 Slot0 */
    rc = firmware_verify_from_flash(EXT_FLASH_SLOT0_ADDR, &hdr,
                                    FW_MAGIC_APP, flash_external_read);
    if (rc != FW_LOADER_SUCCESS) {
        menu_printf("ERROR: No valid application found in Slot0! (rc=%d)\n", rc);
        menu_printf("Please update application first (Option 3).\n");
        return;
    }

    menu_printf("Application verified OK\n");

    /* 跳转到应用（固件头之后） */
    jump_to_app(EXT_FLASH_SLOT0_ADDR);
}
