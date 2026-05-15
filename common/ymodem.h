/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef YMODEM_H
#define YMODEM_H

#include <zephyr/kernel.h>
#include <stdbool.h>


typedef int (*ymodem_block_write_t)(uint32_t addr, const uint8_t *data,
				    size_t len, uint32_t total,
				    int file_size, void *priv);

struct ymodem_config {
	const struct device *dev;
	uint32_t flash_addr;
	uint32_t max_size;
	ymodem_block_write_t block_write_cb;
	void *priv;
};

int ymodem_recv(const struct ymodem_config *config);

#endif /* YMODEM_H */
