/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MZM_SWITCH_SCREEN_OVERLAY_H
#define MZM_SWITCH_SCREEN_OVERLAY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { SWITCH_SCREEN_WIDTH = 1280, SWITCH_SCREEN_HEIGHT = 720,
       SWITCH_UI_WIDTH = 320, SWITCH_UI_HEIGHT = 240 };

typedef struct {
    bool visible;
    bool previous_r3;
} SwitchOverlay;

typedef struct {
    const uint32_t *pixels; /* RGBA8888, red in the low byte */
    int width, height;
    size_t pitch;          /* pixels per row, including padding */
} SwitchSurface;

void SwitchOverlay_ToastBadge(const uint32_t *badge);
void SwitchOverlay_Toast(const char *title,const char *detail);
bool SwitchOverlay_FpsVisible(void);
void SwitchOverlay_ShowFps(bool visible);
void SwitchOverlay_SetFps(unsigned fps);
unsigned SwitchOverlay_Layout(void);
unsigned SwitchOverlay_Scaling(void);
void SwitchOverlay_SetOptions(unsigned layout,unsigned scaling);
void SwitchOverlay_LoadOptions(void);
void SwitchOverlay_SaveOptions(void);
void SwitchOverlay_SecondBounds(int *x,int *y,int *width,int *height);
void SwitchOverlay_Update(SwitchOverlay *overlay, bool r3_held);
bool SwitchOverlay_MapPoint(const SwitchOverlay *overlay, int x, int y,
                            int *ui_x, int *ui_y);
bool SwitchOverlay_Compose(const SwitchOverlay *overlay, uint32_t *output,
                           size_t output_pitch, const SwitchSurface *main_screen,
                           const SwitchSurface *second_screen);
#endif
