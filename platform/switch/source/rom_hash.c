/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rom_hash.h"
#include "port_rom.h"
#include "md5.h"
static unsigned char digest[16]; static char text[33]; static const void *last_rom;
const unsigned char *RomHash_Digest(void) {
    if (last_rom!=gRomData) {
        md5_state_t state; md5_init(&state); md5_append(&state,gRomData,gRomSize); md5_finish(&state,digest);
        for (unsigned i=0;i<16;++i) { text[i*2]="0123456789abcdef"[digest[i]>>4]; text[i*2+1]="0123456789abcdef"[digest[i]&15]; }
        text[32]=0; last_rom=gRomData;
    }
    return digest;
}
const char *RomHash_String(void) { RomHash_Digest(); return text; }
