#!/usr/bin/env python3
"""
fw_summary.py - 打印固件头摘要信息

用法：
    python3 fw_summary.py <firmware.bin>
"""

import struct
import sys


def print_summary(path):
    with open(path, 'rb') as f:
        d = f.read()

    magic = struct.unpack_from('<I', d, 0x00)[0]
    magic_map = {0x53504C53: 'SPL', 0x54504C54: 'TPL', 0x41505041: 'APP'}
    magic_str = magic_map.get(magic, f'UNKNOWN(0x{magic:08X})')

    ver_raw = struct.unpack_from('<I', d, 0x14)[0]
    ver_major = (ver_raw >> 16) & 0xFF
    ver_minor = (ver_raw >> 8) & 0xFF
    ver_patch = ver_raw & 0xFF

    exec_mode = struct.unpack_from('<I', d, 0x0C)[0]
    mode_str = 'XIP' if exec_mode == 1 else 'RAM' if exec_mode == 2 else f'UNKNOWN(0x{exec_mode:08X})'

    bd = d[0x40:0x50].decode('utf-8', errors='replace').rstrip('\0')
    bt = d[0x50:0x58].decode('utf-8', errors='replace').rstrip('\0')

    print("========================================")
    print("  TPL Firmware Header Summary")
    print("========================================")
    print(f"  File:          {path}")
    print(f"  Size:          {len(d)} bytes")
    print(f"  Type:          {magic_str}")
    print(f"  Firmware Size: {struct.unpack_from('<I', d, 0x08)[0]} bytes")
    print(f"  Exec Mode:     {mode_str}")
    print(f"  Load Addr:     0x{struct.unpack_from('<I', d, 0x10)[0]:08X}")
    print(f"  Version:       v{ver_major}.{ver_minor}.{ver_patch}")
    print(f"  Vector Offset: 0x{struct.unpack_from('<I', d, 0x18)[0]:04X}")
    print(f"  Build Date:    {bd}")
    print(f"  Build Time:    {bt}")
    print(f"  Checksum:      0x{struct.unpack_from('<I', d, 0x58)[0]:08X}")
    print(f"  Header CRC:    0x{struct.unpack_from('<I', d, 0x60)[0]:08X}")
    print("========================================")


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <firmware.bin>")
        sys.exit(1)
    print_summary(sys.argv[1])
