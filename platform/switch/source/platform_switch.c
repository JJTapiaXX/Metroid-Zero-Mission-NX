/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <switch.h>
#include "platform_switch.h"

static Framebuffer framebuffer;
static PadState pad;
static SwitchOverlay overlay;
static bool ready, pointer_was_held;
static int cursor_x = 160, cursor_y = 120;
static uint64_t frame_deadline_ns,fps_start;
static unsigned fps_frames;

bool PlatformSwitch_Init(void) {
    if (ready) return true;
    Result rc = framebufferCreate(&framebuffer, nwindowGetDefault(),
        SWITCH_SCREEN_WIDTH, SWITCH_SCREEN_HEIGHT, PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(rc)) return false;
    rc = framebufferMakeLinear(&framebuffer);
    if (R_FAILED(rc)) { framebufferClose(&framebuffer); return false; }
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
    overlay = (SwitchOverlay){0};
    pointer_was_held = false;
    cursor_x = 160; cursor_y = 120;
    frame_deadline_ns = 0; fps_start=0; fps_frames=0;
    ready = true;
    return true;
}

void PlatformSwitch_Exit(void) {
    if (ready) framebufferClose(&framebuffer);
    ready = false;
}

SwitchInput PlatformSwitch_Poll(void) {
    SwitchInput input = {0};
    if (!ready || !appletMainLoop()) { input.quit = true; return input; }
    padUpdate(&pad);
    uint64_t held = padGetButtons(&pad);
    uint64_t down = padGetButtonsDown(&pad);
    bool popup=SwitchOverlay_Layout()==0;
    if (popup) SwitchOverlay_Update(&overlay,(held & HidNpadButton_StickR)!=0);
    else { overlay.visible=false; overlay.previous_r3=(held & HidNpadButton_StickR)!=0; }
    input.overlay_visible=overlay.visible || !popup;
    if (overlay.visible) {
        if (down & HidNpadButton_L) input.tab_delta=-1;
        if (down & HidNpadButton_R) input.tab_delta=1;
        if (held & HidNpadButton_AnyLeft) cursor_x -= 3;
        if (held & HidNpadButton_AnyRight) cursor_x += 3;
        if (held & HidNpadButton_AnyUp) cursor_y -= 3;
        if (held & HidNpadButton_AnyDown) cursor_y += 3;
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_y < 0) cursor_y = 0;
        if (cursor_x >= SWITCH_UI_WIDTH) cursor_x = SWITCH_UI_WIDTH - 1;
        if (cursor_y >= SWITCH_UI_HEIGHT) cursor_y = SWITCH_UI_HEIGHT - 1;
        input.pointer_x = cursor_x; input.pointer_y = cursor_y;
        input.pointer_held = (held & HidNpadButton_A) != 0;
        input.pointer_pressed = (down & HidNpadButton_A) != 0;
    }
    if (!overlay.visible) {
        static const struct { uint64_t nx; uint16_t gba; } keys[] = {
            {HidNpadButton_A, 1}, {HidNpadButton_B, 2},
            {HidNpadButton_Minus, 4}, {HidNpadButton_Plus, 8},
            {HidNpadButton_AnyRight, 16}, {HidNpadButton_AnyLeft, 32},
            {HidNpadButton_AnyUp, 64}, {HidNpadButton_AnyDown, 128},
            {HidNpadButton_R | HidNpadButton_ZR, 256},
            {HidNpadButton_L | HidNpadButton_ZL, 512},
        };
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
            if (held & keys[i].nx) input.gba_keys |= keys[i].gba;
    }
    if (input.overlay_visible) {
        HidTouchScreenState touch = {0};
        unsigned count=hidGetTouchScreenStates(&touch,1) && touch.count>0 ? (unsigned)touch.count:0;
        int px=count ? (int)touch.touches[0].x:0,py=count ? (int)touch.touches[0].y:0;
        int px2=count>1 ? (int)touch.touches[1].x:0,py2=count>1 ? (int)touch.touches[1].y:0;
        bool held=false,pressed=false,save=false;
        bool custom=SwitchOverlay_CornerTouch(count,px,py,px2,py2,
            armTicksToNs(armGetSystemTick())/1000000,&px,&py,&held,&pressed,&save);
        if (save) SwitchOverlay_SaveOptions();
        if ((custom ? held:count>0) && SwitchOverlay_MapPoint(&overlay,px,py,&input.pointer_x,&input.pointer_y)) {
            cursor_x=input.pointer_x; cursor_y=input.pointer_y;
            input.pointer_held=true;
            input.pointer_pressed=custom ? pressed:!pointer_was_held;
        }
    }
    input.touch_released = pointer_was_held && !input.pointer_held;
    pointer_was_held = input.pointer_held;
    return input;
}

bool PlatformSwitch_Present(const SwitchSurface *main_screen,
                            const SwitchSurface *second_screen) {
    if (!ready) return false;
    uint64_t present_time=armTicksToNs(armGetSystemTick());
    if (!fps_start) fps_start=present_time;
    if (++fps_frames && present_time-fps_start>=500000000) {
        SwitchOverlay_SetFps((uint64_t)fps_frames*1000000000/(present_time-fps_start));
        fps_frames=0; fps_start=present_time;
    }
    u32 stride;
    uint32_t *pixels = framebufferBegin(&framebuffer, &stride);
    bool ok = SwitchOverlay_Compose(&overlay, pixels, stride / sizeof(*pixels),
                                   main_screen, second_screen);
    if (ok && overlay.visible && SwitchOverlay_Layout()==0) {
        int left,top,width,height;
        SwitchOverlay_SecondBounds(&left,&top,&width,&height);
        int x=left+cursor_x*width/320,y=top+cursor_y*height/240;
        if (SwitchOverlay_Layout()==2) { x=left+(239-cursor_y)*width/240; y=top+cursor_x*height/320; }
        for (int delta = -5; delta <= 5; ++delta) {
            if (x + delta >= left && x + delta < left+width)
                pixels[(size_t)y * (stride / 4) + x + delta] = 0xffffffff;
            if (y + delta >= top && y + delta < top+height)
                pixels[(size_t)(y + delta) * (stride / 4) + x] = 0xffffffff;
        }
    }
    framebufferEnd(&framebuffer);
    if (ok) {
        /* GBA: 280896 CPU cycles per frame at 16777216Hz. Matching this
         * cadence also keeps the native audio packets aligned with 48kHz. */
        uint64_t now = armTicksToNs(armGetSystemTick());
        if (!frame_deadline_ns || now > frame_deadline_ns + 33485412) {
            frame_deadline_ns = now;
        } else {
            frame_deadline_ns += 16742706;
            if (now < frame_deadline_ns) svcSleepThread(frame_deadline_ns - now);
        }
    }
    return ok;
}
