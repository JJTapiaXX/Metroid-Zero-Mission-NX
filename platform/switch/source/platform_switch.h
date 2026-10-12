/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MZM_PLATFORM_SWITCH_H
#define MZM_PLATFORM_SWITCH_H
#include "screen_overlay.h"

typedef struct {
    uint16_t gba_keys; /* GBA active-high key mask; zero while the popup is open */
    bool quit;
    bool overlay_visible;
    bool pointer_held;
    bool pointer_pressed;
    int pointer_x, pointer_y; /* original 320x240 bottom screen coordinates */
    bool touch_released;
    int8_t tab_delta; /* L/R navigation while second screen has focus */
} SwitchInput;

bool PlatformSwitch_Init(void);
void PlatformSwitch_Exit(void);
SwitchInput PlatformSwitch_Poll(void);
bool PlatformSwitch_Present(const SwitchSurface *main_screen,
                            const SwitchSurface *second_screen);
#endif
