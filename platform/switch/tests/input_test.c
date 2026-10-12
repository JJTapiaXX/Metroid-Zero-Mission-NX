/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <switch.h>
#include "platform_switch.h"

void *memcpy(void *dst,const void *src,size_t count) { unsigned char *d=dst; const unsigned char *s=src; while(count--) *d++=*s++; return dst; }
static uint64_t held, previous;
static int touch_count, touch_x, touch_y;
void *nwindowGetDefault(void) { return 0; }
Result framebufferCreate(Framebuffer *f, void *w, int x, int y, int fmt, int n) {
    (void)f; (void)w; (void)x; (void)y; (void)fmt; (void)n; return 0;
}
Result framebufferMakeLinear(Framebuffer *f) { (void)f; return 0; }
void framebufferClose(Framebuffer *f) { (void)f; }
void *framebufferBegin(Framebuffer *f, u32 *stride) { (void)f; *stride = 0; return 0; }
void framebufferEnd(Framebuffer *f) { (void)f; }
void padConfigureInput(int n, int style) { (void)n; (void)style; }
void padInitializeDefault(PadState *p) { (void)p; }
void padUpdate(PadState *p) { (void)p; }
uint64_t padGetButtons(PadState *p) { (void)p; return held; }
uint64_t padGetButtonsDown(PadState *p) { (void)p; uint64_t down = held & ~previous; previous = held; return down; }
void hidInitializeTouchScreen(void) { }
size_t hidGetTouchScreenStates(HidTouchScreenState *s, size_t n) {
    (void)n; s->count = touch_count; s->touches[0].x = touch_x; s->touches[0].y = touch_y; return 1;
}
bool appletMainLoop(void) { return true; }
uint64_t armGetSystemTick(void) { return 0; }
uint64_t armTicksToNs(uint64_t ticks) { return ticks; }
void svcSleepThread(int64_t ns) { (void)ns; }
/* Name matches the freestanding test runner's entry symbol. */
int overlay_test(void) {
    if (!PlatformSwitch_Init()) return 1;
    held = HidNpadButton_A;
    SwitchInput in = PlatformSwitch_Poll();
    if (in.gba_keys != 1 || in.overlay_visible) return 2;
    held = HidNpadButton_StickR;
    in = PlatformSwitch_Poll();
    if (!in.overlay_visible || in.gba_keys) return 3;
    touch_count = 1; touch_x = 192; touch_y = 24;
    in = PlatformSwitch_Poll();
    if (!in.overlay_visible || !in.pointer_pressed || !in.pointer_held || in.pointer_x || in.pointer_y) return 4;
    touch_x = 640; touch_y = 360;
    in = PlatformSwitch_Poll();
    if (in.pointer_pressed || !in.pointer_held || in.pointer_x != 160 || in.pointer_y != 120) return 5;
    touch_count = 0;
    in = PlatformSwitch_Poll();
    if (!in.touch_released || in.pointer_held) return 6;
    touch_count = 1; touch_x = 1087; touch_y = 695;
    in = PlatformSwitch_Poll();
    if (!in.pointer_pressed || in.pointer_x != 319 || in.pointer_y != 239) return 7;
    touch_x = 1088; /* outside the right edge releases the UI drag */
    in = PlatformSwitch_Poll();
    if (!in.touch_released || in.pointer_held) return 8;
    touch_count=0; held=HidNpadButton_R;
    in=PlatformSwitch_Poll();
    if (in.tab_delta!=1 || in.gba_keys) return 17;
    held=HidNpadButton_L;
    in=PlatformSwitch_Poll();
    if (in.tab_delta!=-1 || in.gba_keys) return 18;
    touch_count = 0;
    held = HidNpadButton_A;
    in = PlatformSwitch_Poll();
    if (!in.pointer_pressed || !in.pointer_held || in.gba_keys) return 9;
    held = HidNpadButton_StickR;
    in = PlatformSwitch_Poll();
    if (in.overlay_visible || !in.touch_released) return 10;
    held = HidNpadButton_B | HidNpadButton_Plus;
    in = PlatformSwitch_Poll();
    if (in.gba_keys != 10 || in.quit) return 11;
    held |= HidNpadButton_Minus;
    in=PlatformSwitch_Poll();
    if (in.quit || in.gba_keys!=14) return 12;
    held=0; touch_count=0;
    SwitchOverlay_SetOptions(1,0);
    PlatformSwitch_Poll();
    touch_count=1; touch_x=1040; touch_y=360;
    in=PlatformSwitch_Poll();
    if (!in.overlay_visible || !in.pointer_pressed || !in.pointer_held || in.gba_keys ||
        in.pointer_x!=160 || in.pointer_y!=120) return 14;
    touch_count=0; held=HidNpadButton_StickR;
    in=PlatformSwitch_Poll();
    if (!in.overlay_visible || !in.touch_released) return 15;
    held=HidNpadButton_A;
    in=PlatformSwitch_Poll();
    if (in.gba_keys!=1 || !in.overlay_visible || in.pointer_held) return 16;
    held=HidNpadButton_A|HidNpadButton_R; touch_count=1;
    in=PlatformSwitch_Poll();
    if (in.gba_keys!=257 || !in.pointer_held || in.tab_delta) return 19;
    SwitchOverlay_SetOptions(2,0);touch_x=16;touch_y=40;
    in=PlatformSwitch_Poll();
    if (in.gba_keys!=257 || !in.pointer_held || in.pointer_x || in.pointer_y!=239) return 20;
    SwitchOverlay_SetOptions(0,0);
    PlatformSwitch_Exit();
    if (!PlatformSwitch_Poll().quit) return 13;
    return 0;
}
