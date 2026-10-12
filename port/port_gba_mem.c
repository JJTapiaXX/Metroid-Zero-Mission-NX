#include "port_gba_mem.h"
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

u8 gIoMem[0x400];
#ifndef PLATFORM_SWITCH
u8 gEwram[0x40000];
#endif /* Switch defines this block and its aliases together in ewram_switch.S. */
u8 gIwram[0x8000];
u16 gBgPltt[256];
u16 gObjPltt[256];
u16 gOamMem[0x400 / 2];
u8 gVram[0x18000];
u8 gSramMem[0x10000];

void gba_write8(uint32_t addr, uint8_t v) {
    if (addr >= 0x02000000u && addr < 0x02040000u) {
        gEwram[addr - 0x02000000u] = v;
        return;
    }
    if (addr >= 0x03000000u && addr < 0x03008000u) {
        gIwram[addr - 0x03000000u] = v;
        return;
    }
    if (addr >= 0x04000000u && addr < 0x04000400u) {
        gIoMem[addr - 0x04000000u] = v;
        return;
    }
    if (addr >= 0x06000000u && addr < 0x06018000u) {
        gVram[addr - 0x06000000u] = v;
        return;
    }
    if (addr >= 0x0E000000u && addr < 0x0E010000u) {
        gSramMem[addr - 0x0E000000u] = v;
        return;
    }

    printf("gba_write8: unimplemented for address 0x%08lX\n", (unsigned long)addr);
}

u8 gba_read8(uint32_t addr) {
    if (addr >= 0x02000000u && addr < 0x02040000u)
        return gEwram[addr - 0x02000000u];
    if (addr >= 0x03000000u && addr < 0x03008000u)
        return gIwram[addr - 0x03000000u];
    if (addr >= 0x04000000u && addr < 0x04000400u)
        return gIoMem[addr - 0x04000000u];
    if (addr >= 0x06000000u && addr < 0x06018000u)
        return gVram[addr - 0x06000000u];
    if (addr >= 0x0E000000u && addr < 0x0E010000u)
        return gSramMem[addr - 0x0E000000u];
    if (gRomData && addr >= 0x08000000u && addr < 0x08000000u + gRomSize) {
#ifndef MZM_3DS
        Port_LogRomAccess(addr, "gba_read8");
#endif
        return gRomData[addr - 0x08000000u];
    }

    printf("gba_read8: unimplemented for address 0x%08lX\n", (unsigned long)addr);
    return 0;
}

void gba_write16(uint32_t addr, uint16_t v) {
    if (addr >= 0x04000000u && addr < 0x040003FFu) {
        gIoMem[addr - 0x04000000u] = v & 0xFF;
        gIoMem[addr - 0x04000000u + 1] = (v >> 8) & 0xFF;
        return;
    }
    if (addr >= 0x02000000u && addr < 0x0203FFFFu) {
        gEwram[addr - 0x02000000u] = v & 0xFF;
        gEwram[addr - 0x02000000u + 1] = (v >> 8) & 0xFF;
        return;
    }
    if (addr >= 0x03000000u && addr < 0x03007FFFu) {
        gIwram[addr - 0x03000000u] = v & 0xFF;
        gIwram[addr - 0x03000000u + 1] = (v >> 8) & 0xFF;
        return;
    }
    // BG palette
    if (addr >= 0x05000000u && addr < 0x050001FFu) {
        gBgPltt[(addr - 0x05000000u) >> 1] = v;
        return;
    }
    // OBJ palette
    if (addr >= 0x05000200u && addr < 0x050003FFu) {
        gObjPltt[(addr - 0x05000200u) >> 1] = v;
        return;
    }
    // VRAM
    if (addr >= 0x06000000u && addr < 0x06017FFFu) {
        u32 off = addr - 0x06000000u;
        gVram[off] = v & 0xFF;
        gVram[off + 1] = (v >> 8) & 0xFF;
        return;
    }
    // OAM
    if (addr >= 0x07000000u && addr < 0x070003FFu) {
        gOamMem[(addr - 0x07000000u) >> 1] = v;
        return;
    }
    if (addr >= 0x0E000000u && addr < 0x0E00FFFEu) {
        u32 off = addr - 0x0E000000u;
        gSramMem[off] = v & 0xFF;
        gSramMem[off + 1] = (v >> 8) & 0xFF;
        return;
    }
    printf("gba_write16: unimplemented for address 0x%08lX\n", (unsigned long)addr);
}

u16 gba_read16(uint32_t addr) {
    if (addr >= 0x04000000u && addr < 0x040003FFu)
        return gIoMem[addr - 0x04000000u] | (gIoMem[addr - 0x04000000u + 1] << 8);
    if (addr >= 0x02000000u && addr < 0x0203FFFFu) {
        u32 off = addr - 0x02000000u;
        return gEwram[off] | (gEwram[off + 1] << 8);
    }
    if (addr >= 0x03000000u && addr < 0x03007FFFu) {
        u32 off = addr - 0x03000000u;
        return gIwram[off] | (gIwram[off + 1] << 8);
    }
    // BG palette
    if (addr >= 0x05000000u && addr < 0x050001FFu)
        return gBgPltt[(addr - 0x05000000u) >> 1];
    // OBJ palette
    if (addr >= 0x05000200u && addr < 0x050003FFu)
        return gObjPltt[(addr - 0x05000200u) >> 1];
    // VRAM
    if (addr >= 0x06000000u && addr < 0x06017FFFu) {
        u32 off = addr - 0x06000000u;
        return gVram[off] | (gVram[off + 1] << 8);
    }
    // OAM
    if (addr >= 0x07000000u && addr < 0x070003FFu)
        return gOamMem[(addr - 0x07000000u) >> 1];
    if (addr >= 0x0E000000u && addr < 0x0E00FFFEu) {
        u32 off = addr - 0x0E000000u;
        return gSramMem[off] | (gSramMem[off + 1] << 8);
    }
    if (gRomData && addr >= 0x08000000u) {
#ifndef MZM_3DS
        Port_LogRomAccess(addr, "gba_read16");
#endif
        u32 off = addr - 0x08000000u;
        if (off + 1 < gRomSize) {
            return gRomData[off] | (gRomData[off + 1] << 8);
        }
    }

    printf("gba_read16: unimplemented for address 0x%08lX\n", (unsigned long)addr);
    return 0;
}

void gba_write32(uint32_t addr, uint32_t v) {
    if (addr >= 0x04000000u && addr < 0x040003FDu) {
        gIoMem[addr - 0x04000000u] = v & 0xFF;
        gIoMem[addr - 0x04000000u + 1] = (v >> 8) & 0xFF;
        gIoMem[addr - 0x04000000u + 2] = (v >> 16) & 0xFF;
        gIoMem[addr - 0x04000000u + 3] = (v >> 24) & 0xFF;
        return;
    }
    // BG palette (0x05000000 - 0x050001FF)
    if (addr >= 0x05000000u && addr < 0x050001FEu) {
        gBgPltt[(addr - 0x05000000u) >> 1] = v & 0xFFFF;
        gBgPltt[((addr - 0x05000000u) >> 1) + 1] = (v >> 16) & 0xFFFF;
        return;
    }
    // BG/OBJ palette boundary straddle
    if (addr == 0x050001FEu) {
        gBgPltt[0xFF] = v & 0xFFFF;
        gObjPltt[0] = (v >> 16) & 0xFFFF;
        return;
    }
    // OBJ palette (0x05000200 - 0x050003FF)
    if (addr >= 0x05000200u && addr < 0x050003FEu) {
        gObjPltt[(addr - 0x05000200u) >> 1] = v & 0xFFFF;
        gObjPltt[((addr - 0x05000200u) >> 1) + 1] = (v >> 16) & 0xFFFF;
        return;
    }
    if (addr >= 0x06000000u && addr < 0x06017FFDu) {
        u32 off = addr - 0x06000000u;
        gVram[off] = v & 0xFF;
        gVram[off + 1] = (v >> 8) & 0xFF;
        gVram[off + 2] = (v >> 16) & 0xFF;
        gVram[off + 3] = (v >> 24) & 0xFF;
        return;
    }
    if (addr >= 0x07000000u && addr < 0x070003FDu) {
        gOamMem[(addr - 0x07000000u) >> 1] = v & 0xFFFF;
        gOamMem[(addr - 0x07000000u + 2) >> 1] = (v >> 16) & 0xFFFF;
        return;
    }
    if (addr >= 0x02000000u && addr < 0x0203FFFDu) {
        u32 off = addr - 0x02000000u;
        gEwram[off] = v & 0xFF;
        gEwram[off + 1] = (v >> 8) & 0xFF;
        gEwram[off + 2] = (v >> 16) & 0xFF;
        gEwram[off + 3] = (v >> 24) & 0xFF;
        return;
    }
    if (addr >= 0x03000000u && addr < 0x03007FFDu) {
        u32 off = addr - 0x03000000u;
        gIwram[off] = v & 0xFF;
        gIwram[off + 1] = (v >> 8) & 0xFF;
        gIwram[off + 2] = (v >> 16) & 0xFF;
        gIwram[off + 3] = (v >> 24) & 0xFF;
        return;
    }
    if (addr >= 0x0E000000u && addr < 0x0E00FFFDu) {
        u32 off = addr - 0x0E000000u;
        gSramMem[off] = v & 0xFF;
        gSramMem[off + 1] = (v >> 8) & 0xFF;
        gSramMem[off + 2] = (v >> 16) & 0xFF;
        gSramMem[off + 3] = (v >> 24) & 0xFF;
        return;
    }
    printf("gba_write32: unimplemented for address 0x%08lX\n", (unsigned long)addr);
}

u32 gba_read32(uint32_t addr) {
    if (addr >= 0x04000000u && addr < 0x040003FDu)
        return gIoMem[addr - 0x04000000u] | (gIoMem[addr - 0x04000000u + 1] << 8) |
               (gIoMem[addr - 0x04000000u + 2] << 16) | (gIoMem[addr - 0x04000000u + 3] << 24);
    if (addr >= 0x05000000u && addr < 0x050001FEu)
        return gBgPltt[(addr - 0x05000000u) >> 1] | (gBgPltt[((addr - 0x05000000u) >> 1) + 1] << 16);
    if (addr >= 0x05000200u && addr < 0x050003FEu)
        return gObjPltt[(addr - 0x05000200u) >> 1] | (gObjPltt[((addr - 0x05000200u) >> 1) + 1] << 16);
    if (addr >= 0x06000000u && addr < 0x06017FFDu) {
        u32 off = addr - 0x06000000u;
        return gVram[off] | (gVram[off + 1] << 8) | (gVram[off + 2] << 16) | (gVram[off + 3] << 24);
    }
    if (addr >= 0x07000000u && addr < 0x070003FDu)
        return gOamMem[(addr - 0x07000000u) >> 1] | (gOamMem[(addr - 0x07000000u + 2) >> 1] << 16);
    if (addr >= 0x02000000u && addr < 0x0203FFFDu) {
        u32 off = addr - 0x02000000u;
        return gEwram[off] | (gEwram[off + 1] << 8) | (gEwram[off + 2] << 16) | (gEwram[off + 3] << 24);
    }
    if (addr >= 0x03000000u && addr < 0x03007FFDu) {
        u32 off = addr - 0x03000000u;
        return gIwram[off] | (gIwram[off + 1] << 8) | (gIwram[off + 2] << 16) | (gIwram[off + 3] << 24);
    }
    if (addr >= 0x0E000000u && addr < 0x0E00FFFDu) {
        u32 off = addr - 0x0E000000u;
        return gSramMem[off] | (gSramMem[off + 1] << 8) | (gSramMem[off + 2] << 16) | (gSramMem[off + 3] << 24);
    }
    if (gRomData && addr >= 0x08000000u) {
#ifndef MZM_3DS
        Port_LogRomAccess(addr, "gba_read32");
#endif
        u32 off = addr - 0x08000000u;
        if (off + 3 < gRomSize) {
            return gRomData[off] | (gRomData[off + 1] << 8) | (gRomData[off + 2] << 16) | (gRomData[off + 3] << 24);
        }
    }

    printf("gba_read32: unimplemented for address 0x%08lX\n", (unsigned long)addr);
    return 0;
}

/* On the 3DS specifically, malloc() can (and does -- confirmed on hardware,
 * gRomData observed at 0x0800eb20) hand back addresses that numerically fall
 * inside the very same 0x08000000-0x0A000000 window this file treats as "raw
 * GBA ROM address, needs translating through gba_TryMemPtr()". gRomData
 * itself is exactly such an allocation, so any ALREADY-RESOLVED pointer into
 * it (e.g. a shim macro like sLanguageSelectGfx, which is
 * *p_sLanguageSelectGfx = gRomData + romOffset) numerically collides with
 * that heuristic. Passing such a pointer back through port_resolve_addr()
 * translates it a SECOND time as if it were a raw GBA address, landing on
 * the wrong ROM bytes entirely -- this was a real, reproduced-on-hardware
 * bug: Lz77Uncomp (port_bios.c) read a bogus decompressed-size from a
 * mistranslated sLanguageSelectGfx pointer and walked its output past the
 * whole program's BSS before faulting (see docs/3ds-port-status-*.md and the
 * commit that added this check). Doesn't reproduce on Linux/x86_64, where
 * malloc() addresses never land in that range.
 *
 * A value inside [gRomData, gRomData+gRomSize) is unambiguous: only
 * already-resolved ROM pointers can equal it (no code constructs a raw GBA
 * address from gRomData's host pointer value), so checking this first and
 * returning unchanged is always correct, on every platform. */
static bool IsWithinRomDataBuffer(uintptr_t val) {
    return gRomData != NULL && val >= (uintptr_t)gRomData && val < (uintptr_t)gRomData + gRomSize;
}

#if defined(PLATFORM_SWITCH)
static bool InGbaNumericRange(uintptr_t val) {
    return (val >= 0x02000000u && val < 0x0A000000u) ||
           (val >= 0x0E000000u && val < 0x0E010000u);
}
static bool IsActiveStackPtr(uintptr_t val) {
    (void)val;
    return false; /* Switch native pointers are outside the 32-bit GBA window. */
}
#elif defined(MZM_3DS)
/* True if the numeric value falls in a numeric range this port treats as a
 * raw GBA address (i.e. it could be a real GBA address to translate, or a
 * host pointer that merely collides numerically with one). */
static bool InGbaNumericRange(uintptr_t val) {
    return (val >= 0x02000000u && val < 0x0A000000u) ||
           (val >= 0x0E000000u && val < 0x0E010000u);
}

/* Host stack pointers can land numerically inside GBA ranges (especially the
 * ROM range 0x08000000..0x08000000+gRomSize, which overlaps the 3DS
 * main-thread stack). Any value that is both in a GBA numeric range AND an
 * active stack address is unambiguously a native host pointer, never a raw
 * GBA address. */
static bool IsActiveStackPtr(uintptr_t val) {
    if (!InGbaNumericRange(val))
        return false;
    extern int Platform3DS_IsActiveStackAddress(uintptr_t value);
    return Platform3DS_IsActiveStackAddress(val) != 0;
}
#else
static bool InGbaNumericRange(uintptr_t val) {
    (void)val;
    return false;
}
static bool IsActiveStackPtr(uintptr_t val) {
    (void)val;
    return false;
}
#endif

void* port_resolve_addr(uintptr_t val)
{
    if (IsWithinRomDataBuffer(val)) {
        return (void*)val;
    }
    /* A value that is currently on the host stack is unambiguously a native
     * pointer, never a raw GBA address -- check this before gba_TryMemPtr,
     * which would otherwise happily "resolve" it into ROM (this is exactly
     * how mzm.sav ended up all zeros on hardware: a live stack address
     * numerically inside the ROM range got translated a second time). */
    if (IsActiveStackPtr(val)) {
        return (void*)val;
    }
    /* GBA-range values (EWRAM/IWRAM/IO/palette/VRAM/OAM/ROM/SRAM) are
     * address-mapped through gba_TryMemPtr; anything outside that window is
     * already a native host pointer (e.g. a local scalar or struct whose
     * address happens to get passed through the same code path as a real
     * GBA address) and is returned unchanged. */
    if (InGbaNumericRange(val)) {
        void* p = gba_TryMemPtr((uint32_t)val);
        if (p) {
            return p;
        }
    }
    return (void*)val;
}

void* port_resolve_write_addr(uintptr_t val) {
    if (IsWithinRomDataBuffer(val)) {
        return (void*)val;
    }
    if (IsActiveStackPtr(val)) {
        return (void*)val;
    }
    /* 0x08000000 and above (except 0x0E000000 SRAM) is read-only cartridge
     * space on GBA. A write destination in that numeric range is therefore a
     * native host pointer, most commonly a 3DS stack or heap object. */
    if ((val >= 0x02000000u && val < 0x08000000u) || (val >= 0x0E000000u && val < 0x0E010000u)) {
        void* p = gba_TryMemPtr((uint32_t)val);
        if (p) return p;
    }
    return (void*)val;
}

const void* port_resolve_copy_src(const void* src, u32 size) {
    (void)size;
    if (IsActiveStackPtr((uintptr_t)src)) {
        return src;
    }
    return port_resolve_addr((uintptr_t)src);
}
