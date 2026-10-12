#ifndef NX_BADGES_H
#define NX_BADGES_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
void Badges_Reset(void);
void Badges_Register(unsigned index,const char *name);
const uint32_t *Badges_Get(unsigned index);
bool Badges_Needs(unsigned index);
int Badges_Next(void);
void Badges_Failed(unsigned index);
bool Badges_Decode(unsigned index,const void *data,size_t size);
#endif
