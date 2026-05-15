/*
 * fw_header.ld - 固件头链接脚本
 *
 * 将 .fw_header 输入段的内容放在 FLASH 的最开头。
 * 通过 CMakeLists.txt 中的 zephyr_linker_sources(ROM_START ...) 包含。
 *
 * 注意：
 * - 不定义新的输出段，只放置输入段内容
 * - 不指定 > FLASH，因为父区域 rom_start 已指定
 * - 通过 zephyr_linker_sources(ROM_START ...) 被包含到
 *   Zephyr 生成的链接脚本的 ROM_START 区域中
 */

KEEP(*(.fw_header))
