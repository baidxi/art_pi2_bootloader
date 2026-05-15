/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "menu.h"
#include "flash_helper.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <stdarg.h>
#include <stdio.h>

/* UART 设备 */
static const struct device *menu_uart_dev = NULL;

/* UART 初始化 */
static void menu_uart_init(void)
{
    if (menu_uart_dev == NULL) {
        menu_uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
        if (!device_is_ready(menu_uart_dev)) {
            printk("Error: UART device not ready\n");
        }
    }
}

int menu_getchar(void)
{
    uint8_t c;

    menu_uart_init();

    if (menu_uart_dev == NULL) {
        return -1;
    }

    /* 使用 uart_poll_in 替代 uart_fifo_read
     * 原因：YModem 协议也使用 uart_poll_in，两者共用 UART 时
     * uart_fifo_read 可能从 FIFO 读取数据导致 uart_poll_in 读不到
     */
    if (uart_poll_in(menu_uart_dev, &c) == 0) {
        return (int)c;
    }

    return -1;
}

void menu_putchar(char c)
{
    menu_uart_init();

    if (menu_uart_dev != NULL) {
        uart_poll_out(menu_uart_dev, c);
    }
}

void menu_puts(const char *str)
{
    while (*str) {
        if (*str == '\n') {
            menu_putchar('\r');
        }
        menu_putchar(*str);
        str++;
    }
}

const struct device *menu_get_uart_dev(void)
{
    menu_uart_init();
    return menu_uart_dev;
}

void menu_printf(const char *fmt, ...)
{
    char buf[256];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    menu_puts(buf);
}

void menu_show_banner(void)
{
    menu_puts("\n");
    menu_puts("========================================\n");
    menu_puts("  ART-Pi2 TPL (Stage 2) Bootloader\n");
    menu_puts("========================================\n");
    menu_puts("TPL Address:     0x70000000\n");
    menu_puts("Slot0 (App):     0x70040000\n");
    menu_puts("Slot1 (Update):  0x70140000\n");
    menu_puts("Storage:         0x70240000\n");
    menu_puts("========================================\n");
    menu_puts("\n");
}

void menu_show_menu(void)
{
    menu_puts("\n");
    menu_puts("========================================\n");
    menu_puts("           Bootloader Menu\n");
    menu_puts("========================================\n");
    menu_puts("  1. Update SPL (Stage 1)\n");
    menu_puts("  2. Update TPL (self)\n");
    menu_puts("  3. Update Application (Stage 3)\n");
    menu_puts("  4. Boot Application\n");
    menu_puts("========================================\n");
    menu_puts("  Select (1-4): ");
}

menu_option_t menu_wait_selection(uint32_t timeout_ms)
{
    int c;
    uint32_t start_time = k_uptime_get_32();

    while (1) {
        c = menu_getchar();

        if (c >= '1' && c <= '4') {
            /* 回显 */
            menu_putchar((char)c);
            menu_putchar('\r');
            menu_putchar('\n');
            return (menu_option_t)c;
        }

        /* 检查超时 */
        if (timeout_ms > 0) {
            if ((k_uptime_get_32() - start_time) >= timeout_ms) {
                return MENU_OPT_INVALID;
            }
        }

        k_yield();
    }
}

bool menu_wait_keypress(uint32_t timeout_ms)
{
    int c;
    uint32_t start_time = k_uptime_get_32();

    while (1) {
        c = menu_getchar();

        if (c >= 0) {
            return true;
        }

        /* 检查超时 */
        if (timeout_ms > 0) {
            if ((k_uptime_get_32() - start_time) >= timeout_ms) {
                return false;
            }
        }

        k_sleep(K_MSEC(10));
    }
}

void menu_process_selection(menu_option_t opt)
{
    switch (opt) {
    case MENU_OPT_UPDATE_SPL:
        menu_puts("Starting SPL update...\n");
        /* update_spl.c 中的函数 */
        {
            extern int update_spl_start(void);
            if (update_spl_start() == 0) {
                menu_puts("SPL update complete. Rebooting...\n");
            } else {
                menu_puts("SPL update failed!\n");
            }
        }
        break;

    case MENU_OPT_UPDATE_TPL:
        menu_puts("Starting TPL update...\n");
        {
            extern int update_tpl_start(void);
            if (update_tpl_start() == 0) {
                menu_puts("TPL update complete. Rebooting...\n");
            } else {
                menu_puts("TPL update failed!\n");
            }
        }
        break;

    case MENU_OPT_UPDATE_APP:
        menu_puts("Starting application update...\n");
        {
            extern int update_app_start(void);
            if (update_app_start() == 0) {
                menu_puts("Application update complete. Rebooting...\n");
            } else {
                menu_puts("Application update failed!\n");
            }
        }
        break;

    case MENU_OPT_BOOT_APP:
        menu_puts("Booting application...\n");
        {
            extern void boot_app(void);
            boot_app();
        }
        break;

    default:
        break;
    }
}
