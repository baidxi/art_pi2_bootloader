#!/usr/bin/env python3
"""
fw_patch.py - 修正编译后的固件 bin 文件中的固件头字段

功能：
1. 修正 firmware_size：实际文件大小
2. 修正 vector_offset：向量表偏移（CONFIG_ROM_START_OFFSET）
3. 修正 checksum：有效代码（固件头之后的数据）的 CRC32
4. 修正 header_crc：固件头自身（前 92 字节）的 CRC32
5. 修正 build_date/build_time 为 YYYY-MM-DD / HH:MM:SS 格式

用法：
    python3 fw_patch.py <firmware.bin> [--vector-offset <hex_offset>]

    如果未指定 --vector-offset，默认使用 0x200（MCUboot 兼容值）。

说明：
    直接修改输入文件，将占位值替换为实际值。
    固件头固定为 128 字节，结构定义见 firmware_header.h。
"""

import struct
import sys
import zlib
import argparse
from datetime import datetime

# 固件头字段偏移（相对于文件开头）
OFFSET_MAGIC          = 0x00
OFFSET_HEADER_VERSION = 0x04
OFFSET_FIRMWARE_SIZE  = 0x08
OFFSET_EXEC_MODE      = 0x0C
OFFSET_LOAD_ADDR      = 0x10
OFFSET_VERSION        = 0x14
OFFSET_VECTOR_OFFSET  = 0x18
OFFSET_RAM_TYPE       = 0x20
OFFSET_RAM_INIT_OFF   = 0x24
OFFSET_RAM_INIT_SIZE  = 0x28
OFFSET_RAM_INIT_LOAD  = 0x2C
OFFSET_RAM_INIT_ENTRY = 0x30
OFFSET_BUILD_DATE     = 0x40
OFFSET_BUILD_TIME     = 0x50
OFFSET_CHECKSUM       = 0x58
OFFSET_HEADER_CRC     = 0x60

HEADER_SIZE = 128
HEADER_CRC_REGION_SIZE = 0x60  # header_crc 之前的字节数（92 字节）

# 占位值
FW_VECTOR_OFFSET_PLACEHOLDER = 0xFFFFFFFF


def patch_firmware(path, vector_offset=0x200):
    with open(path, 'rb') as f:
        data = bytearray(f.read())

    if len(data) < HEADER_SIZE:
        print(f"Error: File too small ({len(data)} bytes)")
        return False

    # 读取魔术字确认是有效固件
    magic = struct.unpack_from('<I', data, OFFSET_MAGIC)[0]
    magic_str = {
        0x53504C53: 'SPL',
        0x54504C54: 'TPL',
        0x41505041: 'APP',
    }.get(magic, 'UNKNOWN')
    print(f"Firmware type: {magic_str} (0x{magic:08X})")

    # 1. 修正 firmware_size
    firmware_size = len(data)
    struct.pack_into('<I', data, OFFSET_FIRMWARE_SIZE, firmware_size)
    print(f"  firmware_size: {firmware_size} bytes")

    # 2. 修正 vector_offset（向量表偏移）
    #    读取当前值，如果是占位符则修正
    current_vector_offset = struct.unpack_from('<I', data, OFFSET_VECTOR_OFFSET)[0]
    if current_vector_offset == FW_VECTOR_OFFSET_PLACEHOLDER:
        struct.pack_into('<I', data, OFFSET_VECTOR_OFFSET, vector_offset)
        print(f"  vector_offset: 0x{vector_offset:04X}")
    else:
        print(f"  vector_offset: 0x{current_vector_offset:04X} (already set, skipped)")

    # 3. 修正 build_date 和 build_time
    #    编译器 __DATE__ 格式为 "MMM DD YYYY"，__TIME__ 格式为 "HH:MM:SS"
    #    固件头中期望 "YYYY-MM-DD" 和 "HH:MM:SS"
    #    这里直接使用当前时间
    now = datetime.now()
    date_str = now.strftime('%Y-%m-%d').encode('utf-8')
    time_str = now.strftime('%H:%M:%S').encode('utf-8')
    # 写入日期字符串并用 \0 填充剩余空间
    data[OFFSET_BUILD_DATE:OFFSET_BUILD_DATE + 16] = date_str.ljust(16, b'\0')
    # 写入时间字符串并用 \0 填充剩余空间
    data[OFFSET_BUILD_TIME:OFFSET_BUILD_TIME + 8] = time_str.ljust(8, b'\0')
    print(f"  build_date: {data[OFFSET_BUILD_DATE:OFFSET_BUILD_DATE+16].decode('utf-8').rstrip(chr(0))}")
    print(f"  build_time: {data[OFFSET_BUILD_TIME:OFFSET_BUILD_TIME+8].decode('utf-8').rstrip(chr(0))}")

    # 4. 修正 checksum（有效代码的 CRC32）
    payload = data[HEADER_SIZE:]  # 固件头之后的所有数据
    checksum = zlib.crc32(payload) & 0xFFFFFFFF
    struct.pack_into('<I', data, OFFSET_CHECKSUM, checksum)
    print(f"  checksum: 0x{checksum:08X}")

    # 5. 修正 header_crc（固件头前 92 字节的 CRC32）
    #    注意：此时 vector_offset 已经修正，所以 CRC 计算包含正确的 vector_offset 值
    header_region = data[:HEADER_CRC_REGION_SIZE]
    header_crc = zlib.crc32(header_region) & 0xFFFFFFFF
    struct.pack_into('<I', data, OFFSET_HEADER_CRC, header_crc)
    print(f"  header_crc: 0x{header_crc:08X}")

    # 写入文件
    with open(path, 'wb') as f:
        f.write(data)

    print(f"Patched successfully: {path}")
    return True


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Patch firmware header fields')
    parser.add_argument('firmware', nargs='+', help='Firmware binary files to patch')
    parser.add_argument('--vector-offset', type=lambda x: int(x, 0), default=0x200,
                        help='Vector table offset (hex), default: 0x200')
    args = parser.parse_args()

    for path in args.firmware:
        if not patch_firmware(path, args.vector_offset):
            sys.exit(1)
