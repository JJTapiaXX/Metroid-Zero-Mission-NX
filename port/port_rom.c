#include "port_rom.h"
#include "generated/port_all_rom_init.h"
#include "port_constructor_init.h"
#include "structs/game_state.h"
#include "constants/game_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

u8* gRomData = NULL;
u32 gRomSize = 0;

/* gRomSize for code that includes <3ds.h>, whose u32 is a different type. */
u32 Port_RomSize(void) {
    return gRomSize;
}
PortRomRegion gRomRegion = PORT_ROM_REGION_UNKNOWN;

/* Game code at ROM header offset 0xAC. Matches Makefile:10,27,44
 * (GAME_CODE = BMXE/BMXP/BMXJ for us/eu/jp). China (BMXC) is commented out
 * upstream (Makefile:52) and not supported here either. */
#define ROM_HEADER_GAME_CODE_OFFSET 0xAC
#define ROM_HEADER_GAME_CODE_SIZE 4
#define ROM_MIN_SIZE (ROM_HEADER_GAME_CODE_OFFSET + ROM_HEADER_GAME_CODE_SIZE)

static PortRomRegion DetectRegion(const u8* data, u32 size) {
    if (size < ROM_MIN_SIZE) {
        return PORT_ROM_REGION_UNKNOWN;
    }
    const u8* code = data + ROM_HEADER_GAME_CODE_OFFSET;
    if (memcmp(code, "BMXE", ROM_HEADER_GAME_CODE_SIZE) == 0) {
        return PORT_ROM_REGION_US;
    }
    if (memcmp(code, "BMXP", ROM_HEADER_GAME_CODE_SIZE) == 0) {
        return PORT_ROM_REGION_EU;
    }
    if (memcmp(code, "BMXJ", ROM_HEADER_GAME_CODE_SIZE) == 0) {
        return PORT_ROM_REGION_JP;
    }
    return PORT_ROM_REGION_UNKNOWN;
}

/*
 * Force gLanguage into a value the currently-loaded ROM's region actually
 * has. mzm only region-clamps gLanguage on new-game init (unk_7584c, via
 * #if REGION_* / INVALID_EU_LANGUAGE); a save shared between region builds
 * can therefore hand SramRead_All() a perfectly valid gLanguage that is
 * wrong for this ROM (e.g. LANGUAGE_ENGLISH loaded on a JP ROM), and the
 * intro that plays right after boot would render with that stale language
 * until some later screen re-clamps it. Returns 1 if it changed gLanguage.
 *
 * Valid sets (see MAKE_ENUM(Language) in constants/game_state.h):
 *   JP : JAPANESE, HIRAGANA          default JAPANESE
 *   US : ENGLISH                     default ENGLISH
 *   EU : ENGLISH..SPANISH            default ENGLISH
 */
int Port_ClampLanguageToRegion(void) {
    s8 lang = (s8)gLanguage;
    s8 want = lang;

    switch (gRomRegion) {
        case PORT_ROM_REGION_JP:
            if (lang != LANGUAGE_JAPANESE && lang != LANGUAGE_HIRAGANA)
                want = LANGUAGE_JAPANESE;
            break;
        case PORT_ROM_REGION_US:
            if (lang != LANGUAGE_ENGLISH)
                want = LANGUAGE_ENGLISH;
            break;
        case PORT_ROM_REGION_EU:
            if (lang < LANGUAGE_ENGLISH || lang >= LANGUAGE_COUNT)
                want = LANGUAGE_ENGLISH;
            break;
        default:
            return 0;
    }

    if (want == lang)
        return 0;
    gLanguage = (Language)want;
    return 1;
}

const char* Port_RomRegionLabel(PortRomRegion region) {
    switch (region) {
        case PORT_ROM_REGION_US:
            return "US";
        case PORT_ROM_REGION_EU:
            return "EU";
        case PORT_ROM_REGION_JP:
            return "JP";
        default:
            return "?";
    }
}

int Port_LoadRom(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    long size = ftell(file);
    if (size < ROM_MIN_SIZE) {
        fclose(file);
        return 0;
    }
    rewind(file);

#if defined(__3DS__) || (defined(MZM_3DS) && !defined(PLATFORM_LINUX))
extern void* linearAlloc(size_t size);
extern void linearFree(void* mem);
#define PORT_ROM_ALLOC(sz) linearAlloc(sz)
#define PORT_ROM_FREE(p) do { if (p) linearFree(p); } while(0)
#else
#define PORT_ROM_ALLOC(sz) malloc(sz)
#define PORT_ROM_FREE(p) do { if (p) free(p); } while(0)
#endif


    u8* buffer = (u8*)PORT_ROM_ALLOC((size_t)size);
    if (!buffer) {
        fclose(file);
        return 0;
    }

    size_t readBytes = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    if (readBytes != (size_t)size) {
        PORT_ROM_FREE(buffer);
        return 0;
    }

    PortRomRegion region = DetectRegion(buffer, (u32)size);
    if (region == PORT_ROM_REGION_UNKNOWN) {
        PORT_ROM_FREE(buffer);
        return 0;
    }

    PORT_ROM_FREE(gRomData);
    gRomData = buffer;
    gRomSize = (u32)size;
    gRomRegion = region;

#ifdef MZM_3DS
    {
        /* Diagnostic: on the 3DS, malloc()'d addresses (gRomData included)
         * can plausibly fall inside the 0x02000000-0x0A000000 window that
         * port_resolve_addr()/port_resolve_copy_src() treat as "raw GBA
         * address, needs translating via gba_TryMemPtr()". If gRomData (or
         * gRomData + a real in-ROM offset like sLanguageSelectGfx) lands in
         * that range, an ALREADY-RESOLVED host pointer gets treated as a
         * raw GBA address and translated AGAIN, landing on the wrong ROM
         * bytes entirely -- a real candidate for the LZ77 header corruption
         * seen in sdmc:/luma/dumps/arm11 (Lz77Uncomp buffer overrun,
         * port_bios.c:139), which doesn't reproduce on Linux/x86_64 where
         * malloc() addresses never collide with that range. */
        extern void Port_DebugLog(const char* msg);
        char msg[128];
        __builtin_snprintf(msg, sizeof(msg), "Port_LoadRom: gRomData=%p (in 0x02..0x0A range: %s)",
            (void*)gRomData,
            ((uintptr_t)gRomData >= 0x02000000u && (uintptr_t)gRomData < 0x0A000000u) ? "YES -- SUSPECT" : "no");
        Port_DebugLog(msg);
    }
#endif
#ifdef PLATFORM_SWITCH
    {
        extern bool Rom64_Init(const uint8_t*, size_t, unsigned);
        if (!Rom64_Init(gRomData, gRomSize, gRomRegion)) {
            PORT_ROM_FREE(gRomData);
            gRomData = NULL;
            gRomSize = 0;
            gRomRegion = PORT_ROM_REGION_UNKNOWN;
            return 0;
        }
    }
#endif
    PortGen_All_Init();
    Port_InitConstructorPointers();
    return 1;
}

/*
 * Minimal access logger: counts distinct ROM addresses seen, so a debug
 * build can report coverage without the overhead of tracking every caller.
 * (Left minimal until there's a concrete use case; exists mainly to
 * satisfy the gba_TryMemPtr() call in port_gba_mem.h).
 */
static u32 sRomAccessCount = 0;

void Port_LogRomAccess(u32 gba_addr, const char* caller) {
    (void)gba_addr;
    (void)caller;
    sRomAccessCount++;
}

void Port_PrintRomAccessSummary(void) {
    printf("ROM access log: %u reads recorded\n", sRomAccessCount);
}
