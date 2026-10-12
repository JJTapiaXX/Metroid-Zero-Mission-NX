/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rom64.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const Rom64Root *root;
    uint8_t *host;
} ConvertedRoot;
static ConvertedRoot *converted;
static size_t converted_count;
static const uint8_t *source_rom;
static size_t source_size;
typedef struct FrameBlock {
    uint32_t address, count;
    uint8_t *host;
    struct FrameBlock *next;
} FrameBlock;
static FrameBlock *frame_blocks;

static uint32_t read32(const uint8_t *src) {
    return (uint32_t)src[0] | (uint32_t)src[1] << 8 |
           (uint32_t)src[2] << 16 | (uint32_t)src[3] << 24;
}

static void decode(uint32_t id, const uint8_t *src, uint8_t *dst) {
    const Rom64Type *type = &gRom64Types[id];
    switch (type->kind) {
    case ROM64_COPY:
        memcpy(dst, src, type->gba_size);
        break;
    case ROM64_POINTER: {
        /* Keep the numeric GBA address. Existing GBA_RESOLVE calls translate it
         * on use. Resolving here would break tagged audio pointer fields. */
        uint64_t address = read32(src);
        memcpy(dst, &address, sizeof(address));
        break;
    }
    case ROM64_ARRAY: {
        const Rom64Field *field = &gRom64Fields[type->first_field];
        const Rom64Type *element = &gRom64Types[field->type];
        for (uint32_t i = 0; i < field->gba_offset; ++i)
            decode(field->type, src + (size_t)i * element->gba_size,
                   dst + (size_t)i * element->host_size);
        break;
    }
    case ROM64_RECORD:
        for (uint32_t i = 0; i < type->field_count; ++i) {
            const Rom64Field *field = &gRom64Fields[type->first_field + i];
            decode(field->type, src + field->gba_offset, dst + field->host_offset);
        }
        break;
    }
}

void Rom64_Exit(void) {
    while (frame_blocks) {
        FrameBlock *next = frame_blocks->next;
        free(frame_blocks->host); free(frame_blocks); frame_blocks = next;
    }
    for (size_t i = 0; i < converted_count; ++i) free(converted[i].host);
    free(converted);
    converted = NULL; converted_count = 0;
    source_rom = NULL; source_size = 0;
}

bool Rom64_Init(const uint8_t *rom, size_t size, unsigned region) {
    Rom64_Exit();
    if (!rom || size < 0xb0 || region < 1 || region > 3) return false;
    converted = calloc(gRom64RootCount, sizeof(*converted));
    if (!converted && gRom64RootCount) return false;
    source_rom = rom; source_size = size;
    for (size_t i = 0; i < gRom64RootCount; ++i) {
        const Rom64Root *root = &gRom64Roots[i];
        if (root->region != region) continue;
        const Rom64Type *type = &gRom64Types[root->type];
        if (root->address < 0x08000000) { Rom64_Exit(); return false; }
        size_t offset = root->address - 0x08000000;
        if (offset > size || type->gba_size > size - offset) { Rom64_Exit(); return false; }
        uint8_t *host = calloc(1, type->host_size);
        if (!host) { Rom64_Exit(); return false; }
        converted[converted_count++] = (ConvertedRoot){root, host};
        decode(root->type, rom + offset, host);
    }
    return true;
}

static bool translate_offset(uint32_t id, uint32_t offset, size_t *host_offset) {
    const Rom64Type *type = &gRom64Types[id];
    if (offset >= type->gba_size) return false;
    if (type->kind == ROM64_COPY || type->kind == ROM64_POINTER) {
        *host_offset = offset; return true;
    }
    if (type->kind == ROM64_ARRAY) {
        const Rom64Field *field = &gRom64Fields[type->first_field];
        const Rom64Type *element = &gRom64Types[field->type];
        size_t inner;
        if (!translate_offset(field->type, offset % element->gba_size, &inner)) return false;
        *host_offset = (size_t)(offset / element->gba_size) * element->host_size + inner;
        return true;
    }
    for (uint32_t i = 0; i < type->field_count; ++i) {
        const Rom64Field *field = &gRom64Fields[type->first_field + i];
        size_t inner;
        if (offset >= field->gba_offset &&
            translate_offset(field->type, offset - field->gba_offset, &inner)) {
            *host_offset = field->host_offset + inner; return true;
        }
    }
    return false; /* Padding has no host data counterpart. */
}

void *Rom64_Resolve(uint32_t address) {
    if (!source_rom || address < 0x08000000 ||
        (size_t)(address - 0x08000000) >= source_size) return NULL;
    for (FrameBlock *frames = frame_blocks; frames; frames = frames->next) {
        uint32_t offset = address - frames->address;
        if (address >= frames->address && offset < frames->count * 8)
            return frames->host + (offset / 8) * 16 + (offset % 8 < 4 ? offset % 8 : offset % 8 + 4);
    }
    /* Roots are sorted by region then address in the generated table. */
    size_t lo = 0, hi = converted_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (converted[mid].root->address <= address) lo = mid + 1;
        else hi = mid;
    }
    if (lo) {
        const ConvertedRoot *block = &converted[lo - 1];
        size_t offset;
        if (translate_offset(block->root->type, address - block->root->address, &offset))
            return block->host + offset;
    }
    return (void *)(source_rom + address - 0x08000000);
}

void *Rom64_ResolveFrames(uintptr_t address) {
    if (address < 0x08000000 || address >= 0x0a000000) return (void *)address;
    uint8_t *resolved = Rom64_Resolve((uint32_t)address);
    if (!resolved) return NULL;
    if ((uintptr_t)resolved < (uintptr_t)source_rom ||
        (uintptr_t)resolved >= (uintptr_t)source_rom + source_size) return resolved;
    size_t remaining = source_size - (resolved - source_rom);
    uint32_t count = 0;
    /* FrameData is an 8-byte GBA record; the zero-duration final record marks
     * the end. Include that sentinel because the animation code reads it. */
    while ((size_t)(count + 1) * 8 <= remaining && count < 1024) {
        const uint8_t *frame = resolved + count++ * 8;
        if (frame[4] == 0) break;
    }
    if (!count || resolved[(count - 1) * 8 + 4] != 0) return NULL;
    FrameBlock *block = calloc(1, sizeof(*block));
    if (!block) return NULL;
    block->host = calloc(count, 16);
    if (!block->host) { free(block); return NULL; }
    block->address = address; block->count = count;
    for (uint32_t i = 0; i < count; ++i) {
        uint64_t pointer = read32(resolved + i * 8);
        memcpy(block->host + i * 16, &pointer, sizeof(pointer));
        block->host[i * 16 + 8] = resolved[i * 8 + 4];
    }
    block->next = frame_blocks; frame_blocks = block;
    return block->host;
}

static bool gba_offset(uint32_t id,size_t host,uint32_t *gba) {
    const Rom64Type *t=&gRom64Types[id];
    if (host>=t->host_size) return false;
    if (t->kind==ROM64_COPY || t->kind==ROM64_POINTER) { if (host>=t->gba_size) return false; *gba=host; return true; }
    if (t->kind==ROM64_ARRAY) {
        const Rom64Field *f=&gRom64Fields[t->first_field]; const Rom64Type *e=&gRom64Types[f->type]; uint32_t inner;
        if (!gba_offset(f->type,host%e->host_size,&inner)) return false;
        *gba=(host/e->host_size)*e->gba_size+inner; return true;
    }
    for (unsigned i=0;i<t->field_count;++i) {
        const Rom64Field *f=&gRom64Fields[t->first_field+i]; uint32_t inner;
        if (host>=f->host_offset && gba_offset(f->type,host-f->host_offset,&inner)) { *gba=f->gba_offset+inner; return true; }
    }
    return false;
}
uint32_t Rom64_Unresolve(const void *pointer,bool *frames) {
    uintptr_t p=(uintptr_t)pointer; *frames=false;
    for (FrameBlock *f=frame_blocks;f;f=f->next) if (p>=(uintptr_t)f->host && p-(uintptr_t)f->host<(size_t)f->count*16) {
        unsigned offset=p-(uintptr_t)f->host; *frames=true;
        return f->address+(offset/16)*8+(offset%16<8 ? offset%16 : offset%16-4);
    }
    for (size_t i=0;i<converted_count;++i) {
        ConvertedRoot *r=&converted[i]; uintptr_t start=(uintptr_t)r->host;
        if (p>=start && p-start<gRom64Types[r->root->type].host_size) {
            uint32_t offset; if (gba_offset(r->root->type,p-start,&offset)) return r->root->address+offset;
        }
    }
    return 0;
}
