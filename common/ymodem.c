#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk-hooks.h>
#include <string.h>
#include <errno.h>

#include "ymodem.h"

/* 协议常量 */
#define SOH   0x01
#define STX   0x02
#define EOT   0x04
#define ACK   0x06
#define NAK   0x15
#define CAN   0x18
#define C     0x43

#define TIMEOUT_CHAR_MS    1000
#define TIMEOUT_START_MS   10000
#define TIMEOUT_INITIAL_MS 3000

static inline void uart_putc(const struct device *dev, uint8_t c)
{
    uart_poll_out(dev, c);
}

static int uart_getc_timeout(const struct device *dev, uint8_t *c, int timeout_ms)
{
    int64_t start = k_uptime_get();
    while (k_uptime_get() - start < timeout_ms) {
        if (uart_poll_in(dev, c) == 0) return 0;
        k_busy_wait(50);
    }
    return -ETIMEDOUT;
}

static void uart_flush(const struct device *dev)
{
    uint8_t c;
    while (uart_poll_in(dev, &c) == 0);
}

static uint16_t crc16_update(uint16_t crc, uint8_t data)
{
    int i;
    crc ^= (uint16_t)data << 8;
    for (i = 0; i < 8; i++) {
        if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
        else crc <<= 1;
    }
    return crc;
}

static uint16_t ymodem_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0;
    while (len--) crc = crc16_update(crc, *data++);
    return crc;
}

static int recv_block_payload(const struct device *dev, uint8_t block_type,
                              uint8_t *data, size_t max_len, size_t *recv_len,
                              uint8_t *block_num)
{
    if (block_type != SOH && block_type != STX) return -EINVAL;

    uint8_t blk, blk_inv;
    if (uart_getc_timeout(dev, &blk, TIMEOUT_CHAR_MS)) return -EIO;
    if (uart_getc_timeout(dev, &blk_inv, TIMEOUT_CHAR_MS)) return -EIO;
    if ((uint8_t)(blk + blk_inv) != 0xFF) return -EBADMSG;

    size_t len = (block_type == STX) ? 1024 : 128;
    if (len > max_len) return -ENOBUFS;

    for (size_t i = 0; i < len; i++)
        if (uart_getc_timeout(dev, &data[i], TIMEOUT_CHAR_MS)) return -EIO;

    uint8_t crc_hi, crc_lo;
    if (uart_getc_timeout(dev, &crc_hi, TIMEOUT_CHAR_MS)) return -EIO;
    if (uart_getc_timeout(dev, &crc_lo, TIMEOUT_CHAR_MS)) return -EIO;

    uint16_t recv_crc = ((uint16_t)crc_hi << 8) | crc_lo;
    if (ymodem_crc16(data, len) != recv_crc) return -EBADMSG;

    *recv_len = len;
    *block_num = blk;
    return 0;
}

static printk_hook_fn_t saved_printk_hook;

static int ymodem_printk_null_hook(int c)
{
    return c;
}

static void ymodem_printk_silence(void)
{
    saved_printk_hook = __printk_get_hook();
    __printk_hook_install(ymodem_printk_null_hook);
}

static void ymodem_printk_restore(void)
{
    if (saved_printk_hook) {
        __printk_hook_install(saved_printk_hook);
        saved_printk_hook = NULL;
    }
}

int ymodem_recv(const struct ymodem_config *config)
{
    const struct device *dev = config->dev;
    uint32_t flash_addr = config->flash_addr;
    uint32_t max_size = config->max_size;
    uint32_t recv_total = 0;
    int file_size = -1;
    char fname[256];
    uint8_t expected_block;
    int ret;

    if (!dev) return -EINVAL;

    ymodem_printk_silence();

    while (1) {
        uart_putc(dev, 'C');

        uint8_t c;
        ret = uart_getc_timeout(dev, &c, TIMEOUT_INITIAL_MS);
        if (ret == 0) {
            if (c == SOH || c == STX) {
                uint8_t block0[1024];
                size_t len;
                uint8_t blk_num;

                int res = recv_block_payload(dev, c, block0, sizeof(block0),
                                             &len, &blk_num);
                if (res == 0 && blk_num == 0) {
                    uart_putc(dev, ACK);
                    int i;
                    for (i = 0; i < len && block0[i] != 0; i++)
                        if (i < sizeof(fname)-1) fname[i] = block0[i];
                    fname[i] = '\0';
                    i++;
                    if (i < len && block0[i] == ' ') {
                        i++;
                        file_size = 0;
                        while (i < len && block0[i] >= '0' && block0[i] <= '9')
                            file_size = file_size*10 + (block0[i++]-'0');
                    }
                    break;
                } else {
                    uart_putc(dev, NAK);
                }
            } else if (c == CAN) {
                uint8_t c2;
                if (uart_getc_timeout(dev, &c2, 100) == 0 && c2 == CAN) {
                    ymodem_printk_restore();
                    return -ECONNABORTED;
                }
            }
        } else {
            uart_flush(dev);
        }
    }

    uart_putc(dev, 'C');
    expected_block = 1;

    while (1) {
        uint8_t c;
        ret = uart_getc_timeout(dev, &c, TIMEOUT_START_MS);
        if (ret) {
            uart_putc(dev, NAK);
            continue;
        }

        if (c == EOT) { uart_putc(dev, ACK); break; }
        if (c == CAN) {
            uint8_t c2;
            if (uart_getc_timeout(dev, &c2, 100) == 0 && c2 == CAN) {
                ymodem_printk_restore();
                return -ECONNABORTED;
            }
            uart_putc(dev, NAK);
            continue;
        }
        if (c != SOH && c != STX) {
            uart_putc(dev, NAK);
            continue;
        }

        uint8_t data[1024];
        size_t len;
        uint8_t blk_num;

        if (recv_block_payload(dev, c, data, sizeof(data), &len, &blk_num) != 0) {
            uart_putc(dev, NAK);
            continue;
        }

        if (blk_num == expected_block) {
            if (recv_total + len > max_size) {
                ymodem_printk_restore();
                return -ENOBUFS;
            }

            if (config->block_write_cb) {
                ret = config->block_write_cb(flash_addr + recv_total, data, len,
                                             recv_total + len, file_size,
                                             config->priv);
                if (ret != 0) {
                    ymodem_printk_restore();
                    return ret;
                }
            }

            recv_total += len;
            uart_putc(dev, ACK);
            expected_block++;
        } else if (blk_num == ((expected_block - 1) & 0xFF)) {
            uart_putc(dev, ACK);
        } else {
            uart_putc(dev, NAK);
        }
    }

    if (file_size >= 0 && recv_total > (uint32_t)file_size)
        recv_total = file_size;

    uart_putc(dev, CAN);
    uart_putc(dev, CAN);

    ymodem_printk_restore();

    return recv_total;
}