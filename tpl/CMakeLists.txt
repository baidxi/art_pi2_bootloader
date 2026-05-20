# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tpl)

# ========================================
# TPL 固件头配置
# ========================================
target_compile_definitions(app PRIVATE
    FW_MAGIC_VALUE=FW_MAGIC_TPL
    FW_LOAD_ADDR=0x70000000
    FW_VERSION_VALUE=0x00010000     # v1.0.0
)

# ========================================
# 源文件
# ========================================
target_sources(app PRIVATE
    src/main.c
    src/menu.c
    src/update_spl.c
    src/update_tpl.c
    src/update_app.c
    src/flash_helper.c
    src/fastboot.c
    ../common/ymodem.c
    ../common/firmware_loader.c
    ../common/fw_header.c
)

# ========================================
# 包含路径
# ========================================
zephyr_include_directories(app PRIVATE_HEADER ../common)

# ========================================
# 固件头链接脚本
# ========================================
# SORT_KEY "#fw_header" 确保固件头在 rom_start_address 之后、fill_with_zeros 之前
# ASCII 排序: "!" < "#fw_header" < "$" < "0x0" < "0x0vectors"
# 这样固件头位于地址 0x0-0x7F，填充到 0x400，向量表在 0x400
zephyr_linker_sources(ROM_START SORT_KEY "#fw_header" ../linker/fw_header.ld)

# ========================================
# 编译后处理：修正固件头 + 打印摘要
# 使用 add_custom_target + add_dependencies 确保在 zephyr_final 之后执行
# ========================================
set(FW_PATCH_SCRIPT "${CMAKE_SOURCE_DIR}/../tools/fw_patch.py")
set(FW_SUMMARY_SCRIPT "${CMAKE_SOURCE_DIR}/../tools/fw_summary.py")
set(ZEPHYR_BIN "${CMAKE_BINARY_DIR}/zephyr/zephyr.bin")

add_custom_target(tpl_patch_fw ALL
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "========================================"
    COMMAND ${CMAKE_COMMAND} -E echo "  Patching TPL firmware header..."
    COMMAND ${CMAKE_COMMAND} -E echo "========================================"
    COMMAND python3 ${FW_PATCH_SCRIPT} --vector-offset 0x400 ${ZEPHYR_BIN}
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND python3 ${FW_SUMMARY_SCRIPT} ${ZEPHYR_BIN}
    COMMENT "Patching TPL firmware header and printing summary"
)

# 依赖 zephyr_final 目标（在 Zephyr 子目录中定义）
# CMake 允许跨目录的 add_dependencies
add_dependencies(tpl_patch_fw zephyr_final)
