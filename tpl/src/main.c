/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file main.c
 * @brief TPL (Tertiary Program Loader, Stage 2) 主程序
 *
 * TPL 运行在外部 Flash (0x70000000)，由 SPL 跳转加载。
 * 功能：
 * 1. 完整硬件初始化
 * 2. 检查并处理升级请求（TPL 自更新、应用更新）
 * 3. 打印启动信息，等待用户按键进入菜单
 * 4. 超时自动启动应用
 * 5. 菜单交互：更新 SPL / 更新 TPL / 更新应用 / 启动应用
 *
 * 固件头验证：
 * - TPL 自身的固件头由 SPL 在跳转前验证
 * - TPL 启动 APP 时使用 firmware_header_verify() 验证 Slot0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(loader, CONFIG_LOG_DEFAULT_LEVEL);

#include "flash_helper.h"

extern int update_tpl_process_request(void);
extern int update_app_process_request(void);
extern void boot_app(void);

#define RED_LED_NODE DT_NODELABEL(red_led)
#define BLUE_LED_NODE DT_NODELABEL(blue_led)
static const struct gpio_dt_spec red_led = GPIO_DT_SPEC_GET_OR(RED_LED_NODE, gpios, {0});
static const struct gpio_dt_spec blue_led = GPIO_DT_SPEC_GET_OR(BLUE_LED_NODE, gpios, {0});

#define MENU_TIMEOUT_MS             3000

static void tpl_init(void);
static void tpl_led_init(void);
static void tpl_led_set(bool red_on, bool blue_on);

int main(void)
{

    tpl_init();

    return 0;
}

static void tpl_init(void)
{
    tpl_led_init();
    tpl_led_set(false, false);

    flash_internal_init();
    flash_external_init();


    printk("TPL initialized\n");
}

static void tpl_led_init(void)
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

static void tpl_led_set(bool red_on, bool blue_on)
{
    if (device_is_ready(red_led.port)) {
        gpio_pin_set_dt(&red_led, red_on ? 1 : 0);
    }

    if (device_is_ready(blue_led.port)) {
        gpio_pin_set_dt(&blue_led, blue_on ? 1 : 0);
    }
}

