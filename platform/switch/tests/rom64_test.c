/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rom64.h"
#include "structs/audio.h"
#include "structs/room.h"
#include <string.h>

static unsigned char arena[1024 * 1024];
static size_t used;
void *calloc(size_t count, size_t size) {
    size_t n = count * size;
    used = (used + 15) & ~(size_t)15;
    if (n > sizeof(arena) - used) return NULL;
    void *result = arena + used;
    memset(result, 0, n);
    used += n;
    return result;
}
void free(void *p) { (void)p; }
void *memset(void *dst, int value, size_t length) {
    unsigned char *bytes = dst;
    for (size_t i = 0; i < length; ++i) bytes[i] = value;
    return dst;
}
void *memcpy(void *dst, const void *src, size_t length) {
    unsigned char *d = dst; const unsigned char *s = src;
    for (size_t i = 0; i < length; ++i) d[i] = s[i];
    return dst;
}
static uint32_t raw32(const uint8_t *s) {
    return (uint32_t)s[0] | (uint32_t)s[1] << 8 | (uint32_t)s[2] << 16 | (uint32_t)s[3] << 24;
}
static uint16_t raw16(const uint8_t *s) { return (uint16_t)s[0] | (uint16_t)s[1] << 8; }

int rom64_test(const uint8_t *rom, size_t size) {
    if (!Rom64_Init(rom, size, 1)) return 1;
    const struct SoundEntry *sounds = Rom64_Resolve(0x0808f2c0);
    for (int i = 0; i < 708; ++i) {
        const uint8_t *raw = rom + 0x8f2c0 + i * 8;
        if ((uintptr_t)sounds[i].pHeader != raw32(raw) ||
            sounds[i].trackGroundNumber != raw16(raw+4) ||
            sounds[i].trackGroundNumber_2 != raw16(raw+6)) return 2;
        if (Rom64_Resolve(0x0808f2c0 + i * 8) != &sounds[i]) return 3;
    }
    const struct TilesetEntry *tilesets = Rom64_Resolve(0x0833dfdc);
    for (int i = 0; i < 79; ++i) {
        const uint8_t *raw = rom + 0x33dfdc + i * 20;
        if ((uintptr_t)tilesets[i].pTileGraphics != raw32(raw) ||
            (uintptr_t)tilesets[i].pPalette != raw32(raw+4) ||
            (uintptr_t)tilesets[i].pBackgroundGraphics != raw32(raw+8) ||
            (uintptr_t)tilesets[i].pTilemap != raw32(raw+12) ||
            tilesets[i].animatedTileset != raw[16] || tilesets[i].animatedPalette != raw[17]) return 4;
    }
    const struct RoomEntryRom *rooms = Rom64_Resolve(0x08340ad4);
    for (int i = 0; i < 42; ++i) {
        const uint8_t *raw = rom + 0x340ad4 + i * 60;
        if (rooms[i].tileset != raw[0] || (uintptr_t)rooms[i].pBg0Data != raw32(raw+8) ||
            (uintptr_t)rooms[i].pBg1Data != raw32(raw+12) ||
            (uintptr_t)rooms[i].pBg2Data != raw32(raw+16) ||
            (uintptr_t)rooms[i].pClipData != raw32(raw+20) ||
            (uintptr_t)rooms[i].pBg3Data != raw32(raw+24) ||
            (uintptr_t)rooms[i].pDefaultSpriteData != raw32(raw+32) ||
            (uintptr_t)rooms[i].pFirstSpriteData != raw32(raw+40) ||
            (uintptr_t)rooms[i].pSecondSpriteData != raw32(raw+48) ||
            rooms[i].mapX != raw[53] || rooms[i].mapY != raw[54] ||
            rooms[i].musicTrack != raw16(raw+58)) return 5;
        if (Rom64_Resolve(0x08340ad4 + i * 60) != &rooms[i]) return 6;
    }
    if (Rom64_Resolve(0x080000ac) != rom + 0xac) return 7;
    if (Rom64_Resolve(0x08000000 + size) || Rom64_Resolve(0x07000000)) return 8;
    Rom64_Exit();
    if (Rom64_Resolve(0x08340ad4)) return 9;
    return 0;
}
