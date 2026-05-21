/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <cmsis_core.h>
#include "flash_helper.h"

#define RED_LED_NODE DT_NODELABEL(red_led)
#define BLUE_LED_NODE DT_NODELABEL(blue_led)
static const struct gpio_dt_spec red_led =
	GPIO_DT_SPEC_GET_OR(RED_LED_NODE, gpios, {0});
static const struct gpio_dt_spec blue_led =
	GPIO_DT_SPEC_GET_OR(BLUE_LED_NODE, gpios, {0});

int main(void)
{
	SCB_EnableICache();
	SCB_EnableDCache();

	if (device_is_ready(red_led.port)) {
		gpio_pin_configure_dt(&red_led, GPIO_OUTPUT);
	}
	if (device_is_ready(blue_led.port)) {
		gpio_pin_configure_dt(&blue_led, GPIO_OUTPUT);
	}

	flash_internal_init();
	flash_external_init();

	printk("TPL initialized\n");

	while (1) {
		k_sleep(K_MSEC(100));
	}
	return 0;
}
