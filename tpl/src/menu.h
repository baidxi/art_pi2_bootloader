/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TPL_MENU_H
#define TPL_MENU_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 菜单选项枚举
 */
typedef enum {
    MENU_OPT_UPDATE_SPL = '1',   /* 更新 SPL */
    MENU_OPT_UPDATE_TPL = '2',   /* 更新 TPL */
    MENU_OPT_UPDATE_APP = '3',   /* 更新应用 */
    MENU_OPT_BOOT_APP   = '4',   /* 启动应用 */
    MENU_OPT_INVALID    = 0      /* 无效选项 */
} menu_option_t;

/**
 * @brief 显示启动信息
 */
void menu_show_banner(void);

/**
 * @brief 显示菜单
 */
void menu_show_menu(void);

/**
 * @brief 等待用户输入并返回选择
 * @param timeout_ms 超时时间（毫秒），0 表示无限等待
 * @return 菜单选项，超时返回 MENU_OPT_INVALID
 */
menu_option_t menu_wait_selection(uint32_t timeout_ms);

/**
 * @brief 处理菜单选择
 * @param opt 菜单选项
 */
void menu_process_selection(menu_option_t opt);

/**
 * @brief 等待按键（带超时）
 * @param timeout_ms 超时时间（毫秒）
 * @return true 有按键输入，false 超时
 */
bool menu_wait_keypress(uint32_t timeout_ms);

/**
 * @brief 从 UART 读取一个字符
 * @return 读取的字符，-1 超时
 */
int menu_getchar(void);

/**
 * @brief 向 UART 发送一个字符
 * @param c 字符
 */
void menu_putchar(char c);

/**
 * @brief 向 UART 发送字符串
 * @param str 字符串
 */
void menu_puts(const char *str);

/**
 * @brief 格式化打印
 * @param fmt 格式字符串
 */
void menu_printf(const char *fmt, ...);

/**
 * @brief 获取 UART 设备指针（用于 YModem 通信）
 * @return UART 设备指针，NULL 表示未就绪
 */
const struct device *menu_get_uart_dev(void);

#endif /* TPL_MENU_H */
