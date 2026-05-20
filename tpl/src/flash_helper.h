/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TPL_FLASH_HELPER_H
#define TPL_FLASH_HELPER_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Flash 地址定义 */
#define FLASH_BASE_ADDR            0x08000000
#define SPL_FLASH_SIZE             (64 * 1024)  /* 64KB 内部 Flash */

#define EXT_FLASH_BASE_ADDR        0x70000000

/* slot0: TPL (外部 Flash offset 0x000000, 1MB) */
#define EXT_FLASH_TPL_ADDR         0x70000000
#define EXT_FLASH_TPL_SIZE         (1 * 1024 * 1024) /* 1MB */
#define EXT_FLASH_SLOT0_ADDR       0x70000000
#define EXT_FLASH_SLOT0_SIZE       (1 * 1024 * 1024) /* 1MB */

/* slot1: APP (外部 Flash offset 0x100000, 1MB) */
#define EXT_FLASH_SLOT1_ADDR       0x70100000
#define EXT_FLASH_SLOT1_SIZE       (1 * 1024 * 1024) /* 1MB */

/* storage (外部 Flash offset 0x600000, 8KB) */
#define EXT_FLASH_STORAGE_ADDR     0x70600000
#define EXT_FLASH_STORAGE_SIZE     (8 * 1024)    /* 8KB */

/* Magic Number 定义 */
#define SPL_MAGIC                  0x53504C53  /* "SPLS" */
#define TPL_MAGIC                  0x54504C54  /* "TPLT" */
#define APP_MAGIC                  0x41505041  /* "APPA" */

/* Flash 操作结果 */
#define FLASH_OP_SUCCESS           0
#define FLASH_OP_ERROR             -1
#define FLASH_OP_VERIFY_ERROR      -2
#define FLASH_OP_TIMEOUT           -3

/* Storage 区域偏移和大小 */
#define STORAGE_OFFSET_UPDATE_REQ  0   /* 升级请求标志 (4字节) */
#define STORAGE_OFFSET_BOOT_COUNT  4   /* 启动计数 (4字节) */
#define STORAGE_SIZE_UPDATE_REQ    4
#define STORAGE_SIZE_BOOT_COUNT    4

/* 升级请求标志值 */
#define UPDATE_REQ_NONE            0x00000000  /* 无升级请求 */
#define UPDATE_REQ_APP             0x41505251  /* "APRQ" - 应用升级请求 */
#define UPDATE_REQ_TPL             0x54505251  /* "TPRQ" - TPL 升级请求 */

/**
 * @brief 初始化内部 Flash
 * @return 0 成功，负数失败
 */
int flash_internal_init(void);

/**
 * @brief 初始化外部 Flash
 * @return 0 成功，负数失败
 */
int flash_external_init(void);

/**
 * @brief 擦除内部 Flash 区域
 * @param addr 起始地址
 * @param len 长度
 * @return 0 成功，负数失败
 */
int flash_internal_erase(uint32_t addr, size_t len);

/**
 * @brief 写入内部 Flash
 * @param addr 起始地址
 * @param data 数据指针
 * @param len 长度
 * @return 0 成功，负数失败
 */
int flash_internal_write(uint32_t addr, const uint8_t *data, size_t len);

/**
 * @brief 读取内部 Flash
 * @param addr 起始地址
 * @param data 数据缓冲区
 * @param len 长度
 * @return 0 成功，负数失败
 */
int flash_internal_read(uint32_t addr, uint8_t *data, size_t len);

/**
 * @brief 擦除外部 Flash 区域
 * @param addr 起始地址
 * @param len 长度
 * @return 0 成功，负数失败
 */
int flash_external_erase(uint32_t addr, size_t len);

/**
 * @brief 写入外部 Flash
 * @param addr 起始地址
 * @param data 数据指针
 * @param len 长度
 * @return 0 成功，负数失败
 */
int flash_external_write(uint32_t addr, const uint8_t *data, size_t len);

/**
 * @brief 读取外部 Flash
 * @param addr 起始地址
 * @param data 数据缓冲区
 * @param len 长度
 * @return 0 成功，负数失败
 */
int flash_external_read(uint32_t addr, uint8_t *data, size_t len);

/**
 * @brief 验证 Flash 区域的 Magic Number
 * @param addr 地址
 * @param magic 期望的 Magic Number
 * @return true 匹配，false 不匹配
 */
bool flash_verify_magic(uint32_t addr, uint32_t magic);

/**
 * @brief 计算 CRC16 校验
 * @param data 数据指针
 * @param len 长度
 * @return CRC16 值
 */
uint16_t flash_calc_crc16(const uint8_t *data, size_t len);

/**
 * @brief 验证 Flash 区域的 CRC
 * @param addr 地址
 * @param len 长度
 * @param expected_crc 期望的 CRC 值
 * @return true 匹配，false 不匹配
 */
bool flash_verify_crc(uint32_t addr, size_t len, uint16_t expected_crc);

/**
 * @brief 从外部 Flash 复制数据到内部 Flash
 * @param dst_addr 目标内部 Flash 地址
 * @param src_addr 源外部 Flash 地址
 * @param len 长度
 * @return 0 成功，负数失败
 */
int flash_copy_ext_to_int(uint32_t dst_addr, uint32_t src_addr, size_t len);

/**
 * @brief 从外部 Flash 复制数据到外部 Flash
 * @param dst_addr 目标外部 Flash 地址
 * @param src_addr 源外部 Flash 地址
 * @param len 长度
 * @return 0 成功，负数失败
 */
int flash_copy_ext_to_ext(uint32_t dst_addr, uint32_t src_addr, size_t len);

/**
 * @brief 写入 Storage 区域
 * @param offset Storage 内偏移
 * @param data 数据指针
 * @param len 长度
 * @return 0 成功，负数失败
 */
int flash_storage_write(uint32_t offset, const uint8_t *data, size_t len);

/**
 * @brief 读取 Storage 区域
 * @param offset Storage 内偏移
 * @param data 数据缓冲区
 * @param len 长度
 * @return 0 成功，负数失败
 */
int flash_storage_read(uint32_t offset, uint8_t *data, size_t len);

/**
 * @brief 设置升级请求标志
 * @param req 升级请求值 (UPDATE_REQ_APP / UPDATE_REQ_TPL / UPDATE_REQ_NONE)
 * @return 0 成功，负数失败
 */
int flash_set_update_request(uint32_t req);

/**
 * @brief 获取升级请求标志
 * @return 升级请求值
 */
uint32_t flash_get_update_request(void);

/**
 * @brief 清除升级请求标志
 * @return 0 成功，负数失败
 */
int flash_clear_update_request(void);

#endif /* TPL_FLASH_HELPER_H */
