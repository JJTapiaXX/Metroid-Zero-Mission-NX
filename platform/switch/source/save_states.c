/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "save_states.h"
#include "native_memory.h"
#include "audio_switch.h"
#include "achievements_switch.h"
#include "rom_hash.h"
#include "port_gba_mem.h"
#include "structs/game_state.h"
#include "structs/audio.h"
#include "constants/game_state.h"
#include "port_psg_synth.h"
#include "virtuappu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static char message[64];
static bool checked[5],exists[5];
extern void PlayCurrentMusicTrack(void);
extern char __data_start[];
extern void Port_FlushSramWait(void);
typedef struct { uint8_t magic[8],signature[16],rom[16]; uint32_t payload_size,checksum,ra_size; uint16_t music,transition; uint8_t priority,reserved[7]; } Header;
typedef struct { unsigned entry,offset; NativePointer pointer; } Relocation;
typedef struct { void *pointer; unsigned size; } Region;
static unsigned checksum(const uint8_t *data,size_t size) { unsigned value=2166136261u; while (size--) value=(value^*data++)*16777619u; return value; }
static void filename(char *path,size_t size,unsigned slot,const char *suffix) { snprintf(path,size,"states/slot-%u%s",slot+1,suffix); }
const char *SaveStates_Message(void) { return message; }
static Region *regions(void) {
    static Region r[8];
    r[0]=(Region){gEwram,sizeof(gEwram)}; r[1]=(Region){gIwram,sizeof(gIwram)};
    r[2]=(Region){gIoMem,sizeof(gIoMem)}; r[3]=(Region){gVram,sizeof(gVram)};
    r[4]=(Region){gBgPltt,sizeof(gBgPltt)}; r[5]=(Region){gObjPltt,sizeof(gObjPltt)};
    r[6]=(Region){gOamMem,sizeof(gOamMem)}; r[7]=(Region){gSramMem,sizeof(gSramMem)}; return r;
}
bool SaveStates_Exists(unsigned slot) {
    if (slot>=5) return false;
    if (checked[slot]) return exists[slot];
    char path[64]; filename(path,sizeof(path),slot,".bin");
    FILE *f=fopen(path,"rb"); checked[slot]=true; exists[slot]=f!=NULL;
    if (f) fclose(f);
    return exists[slot];
}
bool SaveStates_Save(unsigned slot) {
    const NativeHeader *meta=NativeMemory_Header();
    if (slot>=5 || !meta || gMainGameMode!=GM_INGAME || gSubGameMode1!=2) {
        snprintf(message,sizeof(message),"SAVE REQUIRES ACTIVE GAMEPLAY"); return false;
    }
    Port_FlushSramWait(); AudioSwitch_LockState();
    const NativeEntry *entries=NativeMemory_Entries(); const uint32_t *pointers=NativeMemory_Pointers(); Region *r=regions();
    size_t raw_size=0;
    for (unsigned i=0;i<8;++i) raw_size+=r[i].size;
    for (unsigned i=0;i<meta->entries;++i) raw_size+=entries[i].size;
    size_t ra_size=Achievements_ProgressSize(),size=raw_size+4+(size_t)meta->pointers*sizeof(Relocation)+ra_size;
    uint8_t *data=malloc(size); bool okay=data!=NULL; size_t position=0; unsigned count=0;
    if (okay) {
        for (unsigned i=0;i<8;++i) { memcpy(data+position,r[i].pointer,r[i].size); position+=r[i].size; }
        for (unsigned i=0;i<meta->entries;++i) { memcpy(data+position,NativeMemory_Address(entries[i].address),entries[i].size); position+=entries[i].size; }
        position+=4;
        for (unsigned i=0;i<meta->entries && okay;++i) for (unsigned p=0;p<entries[i].pointer_count;++p) {
            unsigned off=pointers[entries[i].pointer_start+p]; uintptr_t value;
            if (off+sizeof(value)>entries[i].size) { okay=false; break; }
            memcpy(&value,(uint8_t*)NativeMemory_Address(entries[i].address)+off,sizeof(value));
            Relocation relocation={i,off,{0,0,0}};
            if (!NativeMemory_Encode(value,&relocation.pointer)) {
                snprintf(message,sizeof(message),"UNSUPPORTED POINTER %.24s",entries[i].name); okay=false; break;
            }
            memcpy(data+position,&relocation,sizeof(relocation)); position+=sizeof(relocation); ++count;
        }
        memcpy(data+raw_size,&count,4);
        if (okay && ra_size && !Achievements_SaveProgress(data+position,ra_size)) okay=false;
        if (okay) position+=ra_size;
    }
    Header header={0}; memcpy(header.magic,"NXSTATE1",8); memcpy(header.signature,meta->signature,16);
    memcpy(header.rom,RomHash_Digest(),16); header.payload_size=position; header.ra_size=ra_size;
    header.music=gMusicInfo.musicTrack; header.transition=gMusicInfo.musicTrackOnTransition; header.priority=gMusicInfo.priority;
    if (okay) header.checksum=checksum((const uint8_t*)&header,sizeof(header))^checksum(data,position);
    AudioSwitch_UnlockState();
    if (okay) {
        mkdir("states",0777); char path[64],temporary[64],backup[64];
        filename(path,sizeof(path),slot,".bin"); filename(temporary,sizeof(temporary),slot,".tmp"); filename(backup,sizeof(backup),slot,".bak");
        FILE *f=fopen(temporary,"wb");
        if (!f) okay=false;
        else { okay=fwrite(&header,1,sizeof(header),f)==sizeof(header) && fwrite(data,1,position,f)==position; int flush=fflush(f); if (fclose(f) || flush) okay=false; }
        if (okay) { remove(backup); rename(path,backup); okay=rename(temporary,path)==0; }
        if (okay) checked[slot]=exists[slot]=true;
        snprintf(message,sizeof(message),okay ? "STATE %u SAVED" : "STATE WRITE FAILED",slot+1);
    }
    free(data); return okay;
}
bool SaveStates_Load(unsigned slot) {
    const NativeHeader *meta=NativeMemory_Header(); if (slot>=5 || !meta) return false;
    char path[64]; filename(path,sizeof(path),slot,".bin"); FILE *f=fopen(path,"rb");
    if (!f) { snprintf(message,sizeof(message),"SLOT %u IS EMPTY",slot+1); return false; }
    Header header; bool okay=fread(&header,1,sizeof(header),f)==sizeof(header);
#ifdef NX_ALLOW_STATE_BUILD_MISMATCH
    bool wrong_build=false;
#else
    bool wrong_build=okay && memcmp(header.signature,meta->signature,16)!=0;
#endif
    if (!okay || memcmp(header.magic,"NXSTATE1",8) || wrong_build ||
        memcmp(header.rom,RomHash_Digest(),16) || header.payload_size>2097152 || header.ra_size>262144) {
        fclose(f); snprintf(message,sizeof(message),"STATE ROM OR BUILD MISMATCH"); return false;
    }
    uint8_t *data=malloc(header.payload_size);
    okay=data && fread(data,1,header.payload_size,f)==header.payload_size && fgetc(f)==EOF;
    fclose(f);
    unsigned expected_checksum=header.checksum; header.checksum=0;
    if (!okay || (checksum((const uint8_t*)&header,sizeof(header))^checksum(data,header.payload_size))!=expected_checksum) {
        free(data); snprintf(message,sizeof(message),"STATE IS INCOMPLETE OR CORRUPT"); return false;
    }
    const NativeEntry *entries=NativeMemory_Entries(); Region *r=regions(); size_t raw_size=0;
    for (unsigned i=0;i<8;++i) raw_size+=r[i].size;
    for (unsigned i=0;i<meta->entries;++i) raw_size+=entries[i].size;
    if (raw_size+4>header.payload_size) okay=false;
    /* RELRO tables carry ELF write flags for relocation but libnx protects
       them after startup. Reject a metadata error before restoring any bytes. */
    for (unsigned i=0;i<meta->entries;++i)
        if ((uintptr_t)NativeMemory_Address(entries[i].address)<(uintptr_t)__data_start) okay=false;
    unsigned count=0; if (okay) memcpy(&count,data+raw_size,4);
    if (count!=meta->pointers || raw_size+4+(size_t)count*sizeof(Relocation)+header.ra_size!=header.payload_size) okay=false;
    Relocation *relocs=okay ? (Relocation*)(data+raw_size+4) : NULL;
    void **decoded=okay ? malloc((size_t)count*sizeof(void*)) : NULL;
    if (okay && count && !decoded) okay=false;
    AudioSwitch_LockState();
    for (unsigned i=0;okay && i<count;++i) {
        Relocation item; memcpy(&item,(uint8_t*)relocs+i*sizeof(item),sizeof(item));
        if (item.entry>=meta->entries || item.offset>entries[item.entry].size || entries[item.entry].size-item.offset<8) { okay=false; break; }
        decoded[i]=NativeMemory_Decode(item.pointer);
        if (item.pointer.value && !decoded[i]) okay=false;
    }
    if (okay) {
        size_t position=0;
        for (unsigned i=0;i<8;++i) { memcpy(r[i].pointer,data+position,r[i].size); position+=r[i].size; }
        for (unsigned i=0;i<meta->entries;++i) { memcpy(NativeMemory_Address(entries[i].address),data+position,entries[i].size); position+=entries[i].size; }
        for (unsigned i=0;i<count;++i) {
            Relocation item; memcpy(&item,(uint8_t*)relocs+i*sizeof(item),sizeof(item));
            memcpy((uint8_t*)NativeMemory_Address(entries[item.entry].address)+item.offset,&decoded[i],sizeof(void*));
        }
        gMusicInfo.musicTrack=header.music; gMusicInfo.musicTrackOnTransition=header.transition; gMusicInfo.priority=header.priority;
        PortPsg_Reset(); PlayCurrentMusicTrack();
        Achievements_LoadProgress(data+raw_size+4+(size_t)count*sizeof(Relocation),header.ra_size);
        gVBlankRequestFlag=1;
        snprintf(message,sizeof(message),"STATE %u LOADED",slot+1);
    } else snprintf(message,sizeof(message),"STATE POINTER VALIDATION FAILED");
    AudioSwitch_UnlockState(); free(decoded); free(data); return okay;
}
