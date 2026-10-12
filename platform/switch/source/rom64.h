/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MZM_SWITCH_ROM64_H
#define MZM_SWITCH_ROM64_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { ROM64_COPY, ROM64_POINTER, ROM64_ARRAY, ROM64_RECORD } Rom64Kind;
typedef struct {
    Rom64Kind kind;
    uint32_t gba_size, host_size, first_field, field_count;
} Rom64Type;
typedef struct { uint32_t gba_offset, host_offset, type; } Rom64Field;
typedef struct { uint32_t region, address, type; } Rom64Root;
extern const Rom64Type gRom64Types[];
extern const Rom64Field gRom64Fields[];
extern const Rom64Root gRom64Roots[];
extern const size_t gRom64RootCount;

bool Rom64_Init(const uint8_t *rom, size_t size, unsigned region);
void Rom64_Exit(void);
/* Returns a converted structure/table, or the original bytes for unchanged data. */
void *Rom64_Resolve(uint32_t gba_address);
uint32_t Rom64_Unresolve(const void *pointer,bool *frames);
void *Rom64_ResolveFrames(uintptr_t address);
#endif
