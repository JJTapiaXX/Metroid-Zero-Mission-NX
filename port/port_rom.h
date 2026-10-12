#pragma once
#include "types.h"
#include <stddef.h>
#include <stdint.h>

/*
 * ROM loading and GBA-address resolution for the native (3DS) port.
 *
 * Unlike the GBA build (which compiles the src/data/ C files directly into
 * the binary via the extracted/ includes), the native port does NOT bake
 * copyrighted assets into the distributed binary. Instead, the user supplies
 * their own legally-dumped ROM at runtime; this file loads it into gRomData
 * and resolves GBA ROM addresses (0x08000000+) to native pointers into that
 * buffer. The per-symbol offset tables that make this work for every
 * src/data/ symbol are generated from the matching build's .map file —
 * see tools/gen_port_rom_offsets.py and docs/3ds-port-rom-loading.md.
 */

typedef enum PortRomRegion {
    PORT_ROM_REGION_UNKNOWN = 0,
    PORT_ROM_REGION_US,
    PORT_ROM_REGION_EU,
    PORT_ROM_REGION_JP,
} PortRomRegion;

/* ROM data buffer (also declared in port_gba_mem.h to avoid a circular
 * include; both declarations must stay in sync). */
extern u8* gRomData;
extern u32 gRomSize;
u32 Port_RomSize(void);

/* Region of the currently loaded ROM. PORT_ROM_REGION_UNKNOWN until a
 * successful Port_LoadRom(). */
extern PortRomRegion gRomRegion;

/*
 * Load and validate a ROM file. Reads the 4-byte game code at header offset
 * 0xAC (BMXE=US, BMXP=EU, BMXJ=JP; matches Makefile:10,27,44 GAME_CODE) to
 * detect the region, sets gRomRegion accordingly, and populates
 * gRomData/gRomSize.
 *
 * Returns 1 on success, 0 on failure (file not found, wrong game code, or
 * allocation failure). Does not verify the SHA-1 — callers that need a
 * stricter check (e.g. to distinguish a clean ROM from a romhack sharing the
 * same game code) should hash gRomData themselves against
 * mzm_{us,eu,jp}.sha1.
 */
int Port_LoadRom(const char* path);

/* Human-readable label for gRomRegion ("US", "EU", "JP", "?"). */
const char* Port_RomRegionLabel(PortRomRegion region);

/*
 * Clamp gLanguage to a value valid for the loaded ROM's region (needed when
 * a save is shared between region builds). Returns 1 if gLanguage changed.
 */
int Port_ClampLanguageToRegion(void);

/*
 * Resolve a GBA ROM address (0x08000000-0x09FFFFFF) to a native pointer
 * into gRomData. Returns NULL if gRomData isn't loaded or the address is
 * out of range.
 */
static inline void* Port_ResolveRomData(u32 gbaAddr) {
#ifdef PLATFORM_SWITCH
    extern void* Rom64_Resolve(uint32_t gba_address);
    return Rom64_Resolve(gbaAddr);
#else
    if (gRomData != NULL && gbaAddr >= 0x08000000u && gbaAddr < 0x08000000u + gRomSize) {
        return &gRomData[gbaAddr - 0x08000000u];
    }
    return NULL;
#endif
}

/* ROM access logging - logs unique ROM addresses accessed at runtime.
 * Declared here (not just in port_gba_mem.h) because port_rom.c owns the
 * implementation. */
void Port_LogRomAccess(u32 gba_addr, const char* caller);
void Port_PrintRomAccessSummary(void);
