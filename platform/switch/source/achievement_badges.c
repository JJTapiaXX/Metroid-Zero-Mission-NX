/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "achievement_badges.h"
#include "../../3ds/source/port_ra_badges_data.c"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 128
#include "../../../third_party/stb/stb_image.h"
static uint32_t images[256][400];
static const uint32_t *badges[256];
static char names[256][32];
static bool tried[256];
void Badges_Reset(void) { memset(badges,0,sizeof(badges)); memset(names,0,sizeof(names)); memset(tried,0,sizeof(tried)); }
static void path(char *out,size_t size,unsigned index,const char *suffix) { snprintf(out,size,"ra/badges/%s%s",names[index],suffix); }
void Badges_Register(unsigned index,const char *name) {
    if (index>=256 || !name || !*name || strlen(name)>=32) return;
    for (const char *p=name;*p;++p) if (*p<'0' || *p>'9') return;
    if (!strcmp(names[index],name)) return;
    snprintf(names[index],32,"%s",name); tried[index]=false;
    badges[index]=Port_RA_GetBundledBadge(name);
    if (badges[index]) return;
    char filename[80];path(filename,sizeof(filename),index,".rgba");FILE *f=fopen(filename,"rb");
    if (f) { if (fread(images[index],1,1600,f)==1600 && fgetc(f)==EOF) badges[index]=images[index]; fclose(f); }
}
const uint32_t *Badges_Get(unsigned index) { return index<256 ? badges[index] : NULL; }
bool Badges_Needs(unsigned index) { return index<256 && names[index][0] && !badges[index] && !tried[index]; }
int Badges_Next(void) { for (unsigned i=0;i<256;++i) if (names[i][0] && !badges[i] && !tried[i]) return (int)i; return -1; }
void Badges_Failed(unsigned index) { if (index<256) tried[index]=true; }
bool Badges_Decode(unsigned index,const void *data,size_t size) {
    if (index>=256 || !names[index][0] || !data || !size || size>262144) return false;
    int w=0,h=0,n=0;unsigned char *rgba=stbi_load_from_memory(data,(int)size,&w,&h,&n,4);
    if (!rgba || w<1 || h<1 || w>128 || h>128) { stbi_image_free(rgba); return false; }
    for (int y=0;y<20;++y) for (int x=0;x<20;++x) {
        unsigned char *p=rgba+((y*h/20)*w+x*w/20)*4;
        images[index][y*20+x]=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
    }
    stbi_image_free(rgba);badges[index]=images[index];
    mkdir("ra",0777);mkdir("ra/badges",0777);
    char filename[80],temporary[80];path(filename,sizeof(filename),index,".rgba");path(temporary,sizeof(temporary),index,".tmp");
    FILE *f=fopen(temporary,"wb");if (f) { bool okay=fwrite(images[index],1,1600,f)==1600;int flush=fflush(f);if (fclose(f) || flush) okay=false; if (okay) { remove(filename);rename(temporary,filename); } }
    return true;
}
