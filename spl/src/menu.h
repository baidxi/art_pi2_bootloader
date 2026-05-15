/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MENU_H
#define MENU_H

#include <stdint.h>

#define NORMAL_BOOT 0
#define UPGRADE_SPL 1
#define UPGRADE_TPL 2
#define UPGRADE_APP 3
#define ANSI_CLEAR          "\033[2J"
#define ANSI_HOME           "\033[H"
#define ANSI_SAVE_CURSOR    "\033[s"
#define ANSI_RESTORE_CURSOR "\033[u"
#define ANSI_REVERSE        "\033[7m"
#define ANSI_RESET          "\033[0m"
#define MENU_ITEM_COUNT  4

int show_menu(uint32_t timeout);

#endif /* MENU_H */
