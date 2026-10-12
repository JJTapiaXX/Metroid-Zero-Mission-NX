/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "native_memory.h"
#include "port_gba_mem.h"
#include "port_rom.h"
#include "rom64.h"
#include <string.h>
extern const uint8_t gNativeMetadata[262144];
extern void _start(void);
static uintptr_t base(void) { return (uintptr_t)_start; }
const NativeHeader *NativeMemory_Header(void) {
    static const NativeHeader *cached; if (cached) return cached;
    const NativeHeader *h=(const NativeHeader*)gNativeMetadata;
    if (memcmp(h->magic,"NXMETA1",7) || h->entries>4000 || h->pointers>32000 || h->maps>2000 || h->copies>16000 ||
        h->entry_offset+(uint64_t)h->entries*sizeof(NativeEntry)>262144 ||
        h->pointer_offset+(uint64_t)h->pointers*4>262144 ||
        h->map_offset+(uint64_t)h->maps*sizeof(NativeMap)>262144 ||
        h->copy_offset+(uint64_t)h->copies*sizeof(NativeField)>262144) return NULL;
    cached=h; return h;
}
const NativeEntry *NativeMemory_Entries(void) { const NativeHeader *h=NativeMemory_Header(); return h ? (const NativeEntry*)(gNativeMetadata+h->entry_offset) : NULL; }
const uint32_t *NativeMemory_Pointers(void) { const NativeHeader *h=NativeMemory_Header(); return h ? (const uint32_t*)(gNativeMetadata+h->pointer_offset) : NULL; }
void *NativeMemory_Address(uint64_t offset) { return (void*)(base()+offset); }
static bool in(uintptr_t p,const void *start,size_t size) { return p>=(uintptr_t)start && p-(uintptr_t)start<size; }
bool NativeMemory_Encode(uintptr_t value,NativePointer *encoded) {
    const NativeHeader *h=NativeMemory_Header(); if (!h) return false;
    *encoded=(NativePointer){value,0,0};
    if (!value || value<0x10000000u) return true; /* Null and numeric GBA pointers. */
    if (in(value,gRomData,gRomSize)) { encoded->kind=2; encoded->value=value-(uintptr_t)gRomData; return true; }
    if (value>=base() && value-base()<h->image_size) { encoded->kind=1; encoded->value=value-base(); return true; }
    bool frames=false; uint32_t address=Rom64_Unresolve((void*)value,&frames);
    if (address) { encoded->kind=frames ? 4 : 3; encoded->value=address; return true; }
    return false; /* A stack/unknown heap pointer cannot survive relaunch. */
}
void *NativeMemory_Decode(NativePointer p) {
    const NativeHeader *h=NativeMemory_Header(); if (!h) return NULL;
    switch (p.kind) {
    case 0:return p.value<0x10000000u ? (void*)(uintptr_t)p.value : NULL;
    case 1:return p.value<h->image_size ? NativeMemory_Address(p.value) : NULL;
    case 2:return p.value<gRomSize ? gRomData+p.value : NULL;
    case 3:return p.value>=0x08000000u && p.value<0x0a000000u ? Rom64_Resolve(p.value) : NULL;
    case 4:return p.value>=0x08000000u && p.value<0x0a000000u ? Rom64_ResolveFrames(p.value) : NULL;
    default:return NULL;
    }
}
static bool gba_pointer(uintptr_t p,uint32_t *out) {
    if (p<0x10000000) { *out=p; return true; }
    bool frames=false; uint32_t raw=Rom64_Unresolve((void*)p,&frames);
    if (raw) { *out=raw; return true; }
    if (in(p,gRomData,gRomSize)) { *out=0x08000000+(p-(uintptr_t)gRomData); return true; }
    if (in(p,gEwram,sizeof(gEwram))) { *out=0x02000000+(p-(uintptr_t)gEwram); return true; }
    const NativeHeader *h=NativeMemory_Header();
    const NativeMap *maps=(const NativeMap*)(gNativeMetadata+h->map_offset);
    for (unsigned i=0;i<h->maps;++i) {
        const NativeField *fields=(const NativeField*)(gNativeMetadata+h->copy_offset)+maps[i].first;
        uintptr_t host=base()+maps[i].address;
        for (unsigned f=0;f<maps[i].count;++f) if (p>=host+fields[f].host && p-host-fields[f].host<fields[f].size) {
            uint32_t flat=maps[i].gba+fields[f].gba+p-host-fields[f].host;
            *out=flat<0x8000 ? 0x03000000+flat : 0x02000000+flat-0x8000; return true;
        }
    }
    return false;
}
bool NativeMemory_ReadByte(uint32_t address,uint8_t *value) {
    const NativeHeader *h=NativeMemory_Header(); if (!h || address>=0x48000) return false;
    const NativeMap *maps=(const NativeMap*)(gNativeMetadata+h->map_offset);
    /* Aliases can overlap, so prefer the smallest matching mapped field. */
    unsigned first=0,last=h->maps;
    while (first<last) { unsigned mid=first+(last-first)/2; if (maps[mid].gba<=address) first=mid+1; else last=mid; }
    unsigned limit=address<0x8000 && first ? first-1 : 0;
    for (unsigned cursor=first;cursor>limit;) { unsigned i=--cursor;
    if (address>=maps[i].gba && address-maps[i].gba<maps[i].size) {
        if (!maps[i].count) return false;
        unsigned offset=address-maps[i].gba;
        const NativeField *fields=(const NativeField*)(gNativeMetadata+h->copy_offset)+maps[i].first;
        for (unsigned f=0;f<maps[i].count;++f) if (offset>=fields[f].gba && offset-fields[f].gba<fields[f].size) {
            const uint8_t *src=(const uint8_t*)NativeMemory_Address(maps[i].address)+fields[f].host;
            unsigned inner=offset-fields[f].gba;
            if (fields[f].pointer) {
                uintptr_t pointer; memcpy(&pointer,src,sizeof(pointer)); uint32_t raw;
                if (!gba_pointer(pointer,&raw)) return false;
                *value=(raw>>(inner*8))&255;
            } else *value=src[inner];
            return true;
        }
        *value=0; return true; /* Original struct padding. */
    }
    }
    if (address>=0x8000) { *value=gEwram[address-0x8000]; return true; }
    return false;
}
