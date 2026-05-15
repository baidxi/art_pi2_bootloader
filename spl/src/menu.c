/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "menu.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>

enum input_state {
    STATE_NORMAL,
    STATE_ESC,
    STATE_ESC_BRACKET,
};

struct menu_item {
    const char *name;
    int id;
};

static const struct menu_item menu_items[] = {
    {"Normal Boot", NORMAL_BOOT},
    {"Update SPL Firmware", UPGRADE_SPL},
    {"Update TPL Firmware", UPGRADE_TPL},
    {"Update Application", UPGRADE_APP},
};

static void draw_menu(int selection, uint32_t remaining)
{
    printk("\033[5;0H");
    printk("\033[K");
    printk("Boot Menu (Auto-select in %us):\n\n", remaining);

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        if (i == selection) {
            printk(ANSI_REVERSE "  [*] %s" ANSI_RESET "\n", menu_items[i].name);
        } else {
            printk("  [ ] %s\n", menu_items[i].name);
        }
    }

    printk("\nUse ↑↓ to select, Enter to confirm");
}

int show_menu(uint32_t timeout)
{
    const struct device *uart_dev;
    int selection = 0;
    enum input_state state = STATE_NORMAL;
    uint64_t start_time;
    uint64_t elapsed;
    uint32_t last_remaining = 0xFFFFFFFF;
    bool timeout_enabled = true;
    unsigned char c;
    int rc;
    
    uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    if (!device_is_ready(uart_dev)) {
        printk("Error: UART device not ready\n");
        return NORMAL_BOOT;
    }

    printk(ANSI_CLEAR);
    printk(ANSI_HOME);

    printk("========================================\n");
    printk("  ART-Pi2 SPL (Stage 1) Bootloader\n");
    printk("========================================\n\n");
    
    printk(ANSI_SAVE_CURSOR);

    start_time = k_uptime_get();

    while (1) {
        elapsed = k_uptime_get() - start_time;
        if (timeout_enabled && elapsed >= timeout) {
            printk("\n\nTimeout - Auto-selecting Normal Boot...\n");
            return NORMAL_BOOT;
        }
        
        uint32_t remaining = timeout_enabled ? (timeout - elapsed) / 1000 : 0;
        
        if (timeout_enabled && remaining != last_remaining) {
            printk(ANSI_RESTORE_CURSOR);
            draw_menu(selection, remaining);
            last_remaining = remaining;
        } else if (!timeout_enabled && last_remaining != 0) {
            printk(ANSI_RESTORE_CURSOR);
            draw_menu(selection, 0);
            last_remaining = 0;
        }
        
        rc = uart_poll_in(uart_dev, &c);
        
        if (rc == 0) {
            switch (state) {
            case STATE_NORMAL:
                if (c == 0x1B) {
                    state = STATE_ESC;
                    timeout_enabled = false;
                } else if (c == '\r' || c == '\n') {
                    printk("\n\nSelected: %s\n", menu_items[selection].name);
                    return menu_items[selection].id;
                } else if (c >= '1' && c <= '4') {
                    int idx = c - '1';
                    if (idx < MENU_ITEM_COUNT) {
                        selection = idx;
                        printk(ANSI_RESTORE_CURSOR);
                        draw_menu(selection, remaining);
                        printk("\n\nSelected: %s\n", menu_items[selection].name);
                        return menu_items[selection].id;
                    }
                } else {
                    timeout_enabled = false;
                }
                break;
                
            case STATE_ESC:
                if (c == '[' || c == 'O') {
                    state = STATE_ESC_BRACKET;
                } else {
                    state = STATE_NORMAL;
                }
                break;
                
            case STATE_ESC_BRACKET:
                if (c == 'A') {
                    selection = (selection > 0) ? selection - 1 : MENU_ITEM_COUNT - 1;
                    printk(ANSI_RESTORE_CURSOR);
                    draw_menu(selection, remaining);
                } else if (c == 'B') {
                    selection = (selection < MENU_ITEM_COUNT - 1) ? selection + 1 : 0;
                    printk(ANSI_RESTORE_CURSOR);
                    draw_menu(selection, remaining);
                }
                state = STATE_NORMAL;
                break;
            }
        }
    }
    return NORMAL_BOOT;
}
