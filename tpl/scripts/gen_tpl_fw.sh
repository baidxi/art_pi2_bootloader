#!/bin/bash
#
# gen_tpl_fw.sh - 生成带修正固件头的 TPL 固件
#
# 用法: ./scripts/gen_tpl_fw.sh [build_dir]
#
# 默认 build_dir: build/art_pi2
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TPL_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
WORKSPACE_DIR="$(cd "$TPL_DIR/../.." && pwd)"

BUILD_DIR="${1:-${TPL_DIR}/build/art_pi2}"
OUTPUT_DIR="${BUILD_DIR}/output"
ZEPHYR_ELF="${BUILD_DIR}/zephyr/zephyr.elf"
TPL_BIN="${OUTPUT_DIR}/tpl.bin"
FW_PATCH="${WORKSPACE_DIR}/tools/fw_patch.py"

echo "Generating TPL firmware with header..."
echo "  ELF:       ${ZEPHYR_ELF}"
echo "  Output:    ${TPL_BIN}"
echo "  Patch:     ${FW_PATCH}"

# 检查 zephyr.elf 是否存在
if [ ! -f "${ZEPHYR_ELF}" ]; then
    echo "ERROR: ${ZEPHYR_ELF} not found!"
    echo "Please run 'west build' first."
    exit 1
fi

# 创建输出目录
mkdir -p "${OUTPUT_DIR}"

# 生成 tpl.bin
arm-zephyr-eabi-objcopy -O binary "${ZEPHYR_ELF}" "${TPL_BIN}"
echo "  Generated: ${TPL_BIN} ($(stat -c%s "${TPL_BIN}") bytes)"

# 修正固件头
python3 "${FW_PATCH}" "${TPL_BIN}"

echo "Done!"
