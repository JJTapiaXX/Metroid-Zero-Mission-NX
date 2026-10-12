/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NX_NATIVE_MEMORY_H
#define NX_NATIVE_MEMORY_H
#include <stdint.h>
#include <stdbool.h>
typedef struct { char magic[8]; uint32_t entries,pointers,maps,copies,entry_offset,pointer_offset,map_offset,copy_offset; uint8_t signature[16]; uint64_t image_size; } NativeHeader;
typedef struct { uint64_t address; uint32_t size,pointer_start,pointer_count,reserved; char name[48]; } NativeEntry;
typedef struct { uint32_t gba,size,address,first,count,reserved; } NativeMap;
typedef struct { uint32_t gba,host,size,pointer; } NativeField;
typedef struct { uint64_t value; uint32_t kind,reserved; } NativePointer;
const NativeHeader *NativeMemory_Header(void);
const NativeEntry *NativeMemory_Entries(void);
const uint32_t *NativeMemory_Pointers(void);
void *NativeMemory_Address(uint64_t offset);
bool NativeMemory_Encode(uintptr_t value,NativePointer *encoded);
void *NativeMemory_Decode(NativePointer encoded);
bool NativeMemory_ReadByte(uint32_t address,uint8_t *value);
#endif
