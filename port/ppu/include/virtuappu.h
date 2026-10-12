/*
 * Software GBA PPU for native ports — GPL-3.0-or-later.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Software GBA PPU, vendored as first-party port source. Derived from
 * VirtuaPPU by Mathéo Vignaud (https://github.com/MatheoVignaud/VirtuaPPU,
 * commit 5cf5e99) and incorporating accuracy and portability patches.
 */

#pragma once

#include <stdint.h>

#include "ppu_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
#if defined(MZM_3DS) || defined(PORT_NATIVE)
    /* Console ports only need the native 240x160 frame. Mode 1 binds the
     * engine's GBA VRAM, so the oversized desktop scratch buffers are unused. */
#ifdef PLATFORM_SWITCH
    VIRTUAPPU_MAX_FRAME_WIDTH = 288,
#else
    VIRTUAPPU_MAX_FRAME_WIDTH = 240,
#endif
    VIRTUAPPU_MAX_FRAME_HEIGHT = 160,
    VIRTUAPPU_VRAM_SIZE = 0x18000,
#else
    VIRTUAPPU_MAX_FRAME_WIDTH = 1280,
    VIRTUAPPU_MAX_FRAME_HEIGHT = 360,
    VIRTUAPPU_VRAM_SIZE = 4 * 1024 * 1024,
#endif
    VIRTUAPPU_FRAME_BUFFER_SIZE = VIRTUAPPU_MAX_FRAME_WIDTH * VIRTUAPPU_MAX_FRAME_HEIGHT
};

extern uint32_t virtuappu_frame_buffer[VIRTUAPPU_FRAME_BUFFER_SIZE];
extern uint8_t virtuappu_vram[VIRTUAPPU_VRAM_SIZE];
extern PPUMemory virtuappu_registers;

void virtuappu_reset(void);
void virtuappu_render_frame(void);
uint32_t *virtuappu_get_frame_buffer(void);
uint8_t *virtuappu_get_vram(void);
PPUMemory *virtuappu_get_registers(void);

#ifdef __cplusplus
}
#endif
