/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/device.h>
#include <zephyr/sys/crc.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include "flash_helper.h"
#include "ymodem.h"
#include "firmware_loader.h"

LOG_MODULE_DECLARE(spl, CONFIG_LOG_DEFAULT_LEVEL);

#define RED_LED_NODE DT_NODELABEL(red_led)
#define BLUE_LED_NODE DT_NODELABEL(blue_led)
static const struct gpio_dt_spec red_led = GPIO_DT_SPEC_GET_OR(RED_LED_NODE, gpios, {0});
static const struct gpio_dt_spec blue_led = GPIO_DT_SPEC_GET_OR(BLUE_LED_NODE, gpios, {0});
static const struct gpio_dt_spec btn0 = GPIO_DT_SPEC_GET(DT_ALIAS(mcuboot_button0), gpios);

static void spl_init(void);
static void spl_led_init(void);
static void spl_led_set(bool red_on, bool blue_on);
static void spl_jump_to_tpl(void);
static void spl_jump_to_address(uint32_t addr);
static int spl_update_mode(bool update_spl);
static void spl_delay_ms(uint32_t ms);

int main(void)
{
    int value;

    spl_init();
    gpio_pin_configure_dt(&btn0, GPIO_INPUT);

    value = gpio_pin_get_dt(&btn0);

    if (value)
    {
        LOG_INF("upgrading TPL");
        spl_update_mode(false);
        k_sleep(K_MSEC(1000));
        sys_reboot(SYS_REBOOT_COLD);
    }
    
    LOG_DBG("Checking TPL...\n");
    
    {
        struct firmware_header tpl_header;
        int rc;
        
        rc = firmware_verify_from_flash(EXT_FLASH_TPL_ADDR, &tpl_header,
                                        FW_MAGIC_TPL, flash_external_read);
        
        if (rc == FW_LOADER_SUCCESS) {
            LOG_DBG("TPL found at 0x%08X\n", EXT_FLASH_TPL_ADDR);
            spl_led_set(false, true);  /* 蓝灯亮 */
            
            LOG_DBG("Jumping to TPL...\n");
            k_sleep(K_MSEC(100));
            
            firmware_load_and_jump(&tpl_header, EXT_FLASH_TPL_ADDR,
                                   flash_external_read);
            
            LOG_ERR("TPL jump failed (rc=%d), entering update mode...", rc);
            spl_led_set(true, true);
            
            if (spl_update_mode(false) == 0) {
                LOG_INF("TPL update successful, rebooting...\n");
            } else {
                LOG_ERR("TPL update failed, rebooting...\n");
            }
            
            k_sleep(K_MSEC(1000));
            sys_reboot(SYS_REBOOT_COLD);
        } else {
            LOG_ERR("TPL not found or invalid! (rc=%d)\n", rc);
            LOG_DBG("Entering TPL update mode...\n");
            spl_led_set(true, true);
            
            if (spl_update_mode(false) == 0) {
                LOG_DBG("TPL update successful, rebooting...\n");
            } else {
                LOG_ERR("TPL update failed, rebooting...\n");
            }
            
            k_sleep(K_MSEC(1000));
            sys_reboot(SYS_REBOOT_COLD);
        }
    }
    
    return 0;
}

static const struct device *g_uart_dev;

static void spl_init(void)
{
    spl_led_init();
    spl_led_set(false, false);
    
    flash_internal_init();
    flash_external_init();

    g_uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    if (!device_is_ready(g_uart_dev)) {
        LOG_ERR("UART device not ready");
        while(1)
        {

        }
    }
    
    LOG_INF("SPL initialized");
}

static void spl_led_init(void)
{
    if (device_is_ready(red_led.port)) {
        gpio_pin_configure_dt(&red_led, GPIO_OUTPUT);
        gpio_pin_set_dt(&red_led, 0);
    }
    
    if (device_is_ready(blue_led.port)) {
        gpio_pin_configure_dt(&blue_led, GPIO_OUTPUT);
        gpio_pin_set_dt(&blue_led, 0);
    }
}

/* LED 控制 */
static void spl_led_set(bool red_on, bool blue_on)
{
    if (device_is_ready(red_led.port)) {
        gpio_pin_set_dt(&red_led, red_on ? 1 : 0);
    }
    
    if (device_is_ready(blue_led.port)) {
        gpio_pin_set_dt(&blue_led, blue_on ? 1 : 0);
    }
}

/* 延时函数 */
static void spl_delay_ms(uint32_t ms)
{
    k_sleep(K_MSEC(ms));
}

struct spl_write_ctx {
    bool update_spl;
    uint32_t flash_addr;
    uint8_t *ram_buf;
};

static int spl_block_write(uint32_t addr, const uint8_t *data,
                           size_t len, uint32_t total,
                           int file_size, void *priv)
{
    struct spl_write_ctx *ctx = (struct spl_write_ctx *)priv;
    int rc;

    if (ctx->ram_buf) {
        memcpy(ctx->ram_buf + total - len, data, len);
        return 0;
    }

    if (ctx->update_spl) {
        rc = flash_internal_write(ctx->flash_addr + total - len, data, len);
    } else {
        rc = flash_external_write(ctx->flash_addr + total - len, data, len);
    }

    return rc;
}

static int spl_update_mode(bool update_spl)
{
    struct ymodem_config config;
    struct spl_write_ctx wctx;
    uint32_t flash_addr;
    uint32_t max_size;
    uint8_t *ram_buf = NULL;
    int rc;
    
    if (update_spl) {
        flash_addr = FLASH_BASE_ADDR;
        max_size = SPL_FLASH_SIZE;
        LOG_INF("Send SPL firmware via YModem (max %d KB)...", max_size / 1024);
    } else {
        flash_addr = EXT_FLASH_TPL_ADDR;
        max_size = EXT_FLASH_TPL_SIZE;
        LOG_INF("Send TPL firmware via YModem (max %d KB)...", max_size / 1024);
    }

    ram_buf = k_malloc(max_size);
    if (ram_buf) {
        LOG_INF("Using RAM buffer at 0x%08X for firmware reception\n",
               (uint32_t)ram_buf);
        memset(ram_buf, 0, max_size);
    } else {
        LOG_ERR("RAM buffer allocation failed, writing directly to Flash\n");
        return -1;
    }

    wctx.update_spl = update_spl;
    wctx.flash_addr = flash_addr;
    wctx.ram_buf = ram_buf;

    config.dev = g_uart_dev;
    config.flash_addr = (uint32_t)ram_buf ? (uint32_t)ram_buf : flash_addr;
    config.max_size = max_size;
    config.block_write_cb = spl_block_write;
    config.priv = &wctx;

    rc = ymodem_recv(&config);
    
    if (rc < 0) {
        LOG_ERR("Error: YModem receive failed (%d)", rc);
        if (ram_buf) k_free(ram_buf);
        return -1;
    }
    
    LOG_DBG("Received %u bytes\n", rc);

    {
        const struct firmware_header *fw_header;
        int vrc;

        fw_header = (const struct firmware_header *)(ram_buf ? ram_buf : (void *)(uintptr_t)flash_addr);
        vrc = firmware_header_verify(fw_header,
                                     update_spl ? FW_MAGIC_SPL : FW_MAGIC_TPL);
        
        if (vrc == FW_LOADER_SUCCESS) {
            LOG_INF("Firmware header verified OK");
            
            {
                uint32_t payload_size;
                const uint8_t *payload;
                
                payload_size = fw_header->firmware_size - FIRMWARE_HEADER_SIZE;
                payload = (const uint8_t *)fw_header + FIRMWARE_HEADER_SIZE;
                
                if (payload_size > 0 && payload_size <= max_size) {
                    vrc = firmware_verify_checksum(fw_header, payload);
                    if (vrc == FW_LOADER_SUCCESS) {
                        LOG_INF("Firmware checksum verified OK (0x%08X)",
                               fw_header->checksum);
                    } else {
                        LOG_ERR("Warning: Firmware checksum verification failed (rc=%d)", vrc);
                    }
                    LOG_INF("Firmware verified successfully (%u bytes payload)",
                           payload_size);
                }
            }
        } else {
            LOG_ERR("Warning: Firmware header verification failed (rc=%d)", vrc);
            LOG_ERR("  The firmware may be invalid or corrupted\n");
            if (ram_buf) k_free(ram_buf);
            return -1;
        }
    }
    
    if (ram_buf) {
        uint32_t write_size = (uint32_t)rc;
        
        LOG_DBG("Writing %u bytes to Flash at 0x%08X...", write_size, flash_addr);
    
        if (update_spl) {
            rc = flash_internal_erase(flash_addr, write_size);
        } else {
            rc = flash_external_erase(flash_addr, write_size);
        }
        
        if (rc != 0) {
            LOG_ERR("Error: Flash erase failed (rc=%d)", rc);
            k_free(ram_buf);
            return -1;
        }

        {
            uint32_t offset = 0;
            const uint32_t chunk_size = 256; /* 每块 256 字节 */
            
            while (offset < write_size) {
                uint32_t chunk = (write_size - offset > chunk_size) ?
                                 chunk_size : (write_size - offset);
                
                if (update_spl) {
                    rc = flash_internal_write(flash_addr + offset,
                                              ram_buf + offset, chunk);
                } else {
                    rc = flash_external_write(flash_addr + offset,
                                              ram_buf + offset, chunk);
                }
                
                if (rc != 0) {
                    LOG_ERR("Error: Flash write failed at offset 0x%X (rc=%d)",
                           offset, rc);
                    k_free(ram_buf);
                    return -1;
                }
                
                offset += chunk;
            }
        }
        
        LOG_DBG("Flash write complete\n");
        
        k_free(ram_buf);
    }
    
    return 0;
}

static void spl_jump_to_tpl(void)
{
    spl_jump_to_address(EXT_FLASH_TPL_ADDR);
}

static void spl_jump_to_address(uint32_t addr)
{
    uint32_t *vector_table = (uint32_t *)addr;
    uint32_t stack_ptr;
    uint32_t reset_handler;
    void (*jump_to_app)(void);
    
    __disable_irq();

    SysTick->CTRL = 0;
    SysTick->VAL = 0;
    
    SCB->VTOR = addr;

    stack_ptr = vector_table[0];
    reset_handler = vector_table[1];
    
    if (!((stack_ptr >= 0x20000000 && stack_ptr <= 0x20050000) ||
          (stack_ptr >= 0x24000000 && stack_ptr <= 0x24080000))) {
        LOG_ERR("Error: Invalid stack pointer: 0x%08X", stack_ptr);
        return;
    }
    
    if (reset_handler < addr || reset_handler > addr + 0x100000) {
        LOG_ERR("Error: Invalid reset handler: 0x%08X", reset_handler);
        return;
    }
    
    __set_MSP(stack_ptr);
    
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
    }
    
    jump_to_app = (void (*)(void))reset_handler;
    jump_to_app();
}
