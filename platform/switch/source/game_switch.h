/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GAME_SWITCH_H
#define GAME_SWITCH_H
#include <stdbool.h>
#include <stdint.h>
bool GameSwitch_Load(const char *path);
void GameSwitch_Run(void);
void GameSwitch_Shutdown(void);
void GameSwitch_VBlank(void);
void GameSwitch_Scanline(unsigned line);
void GameSwitch_RequestQuit(void);
#endif
