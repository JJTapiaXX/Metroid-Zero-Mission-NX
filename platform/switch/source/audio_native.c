/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Portable counterparts of the remaining audio_internal/soundcode routines.
 * Field accesses deliberately use C layouts rather than ARM32 byte offsets. */
#include "audio.h"
#include "audio/track_internal.h"
#include "data/audio.h"
#include "port_gba_mem.h"
#include "syscalls.h"
#include "port_psg_synth.h"
#include "audio_switch.h"
#include "audio_volume.h"
#include "audio_filters.h"
#include "diagnostics.h"
#include <string.h>
#include <stdint.h>

void DoSoundAction(u32 action);
static uint32_t raw32(const uint8_t *s) {
    return (uint32_t)s[0] | (uint32_t)s[1]<<8 | (uint32_t)s[2]<<16 | (uint32_t)s[3]<<24;
}
void NativeAudio_GetVoice(const struct Voice *group, unsigned index, struct Voice *voice) {
    const uint8_t *raw = port_resolve_addr((uintptr_t)group);
    raw += index * 12;
    voice->instrumentType=raw[0]; voice->pitch=raw[1]; voice->unk_2=raw[2]; voice->unk_3=raw[3];
    voice->pSample=(u32*)(uintptr_t)raw32(raw+4);
    memcpy(&voice->envelope,raw+8,4);
}
void ResetTrack(struct TrackData *track) {
    if ((track->unk_1E&1) || !(track->flags&2)) return;
    track->flags=1; track->currentTrack=0;
    for (unsigned i=0;i<track->amountOfTracks;++i) {
        struct TrackVariables *v=&track->pVariables[i];
        if (v->pSoundPsg) {
            ClearRegistersForPsg(v->pSoundPsg,(v->channel&7)-1);
            v->pSoundPsg->unk_0=0; v->pSoundPsg->pVariables=NULL;
        }
        for (struct SoundChannel *c=v->pChannel;c;) {
            struct SoundChannel *next=c->pChannel2;
            c->unk_0=0; c->pVariables=NULL; c->pChannel1=NULL; c->pChannel2=NULL; c=next;
        }
        v->pChannel=NULL; v->pSoundPsg=NULL;
    }
}
void StopMusicOrSound(struct TrackData *track) {
    if (track->occupied) return;
    track->occupied=1; ResetTrack(track); track->occupied=0;
}
u8 InitTrack(struct TrackData *track,const u32 *header) {
    const uint8_t *raw=port_resolve_addr((uintptr_t)header);
    if (!raw || track->occupied) return 0;
    track->occupied=1;
    if (track->unk_1E&1) { track->occupied=0; return 0; }
    if (!raw[0]) { ResetTrack(track); track->flags=0; track->occupied=0; return 0; }
    if ((track->flags&2) && track->unk_1D && track->unk_3>raw[2]) {
        track->occupied=0; return 1;
    }
    ResetTrack(track);
    unsigned count=raw[0];
    if (count>track->maxAmountOfTracks) count=track->maxAmountOfTracks;
    track->flags=2; track->amountOfTracks=count; track->undefined_2=raw[1]; track->unk_3=raw[2];
    track->pVoice=(struct Voice*)(uintptr_t)raw32(raw+4);
    track->pHeader=(const u8*)header; track->unk_C=256; track->unk_E=0;
    if (raw[3]&0x80) DoSoundAction(raw[3]);
    memset(track->pVariables,0,count*sizeof(*track->pVariables));
    for (unsigned i=0;i<count;++i) {
        struct TrackVariables *v=&track->pVariables[i];
        v->unk_0=1; v->unk_5=64; v->unk_6=64; v->bendRange=2; v->unk_1B=12;
        v->pRawData=port_resolve_addr(raw32(raw+8+i*4));
    }
    track->occupied=0;
    return 0;
}
u32 unk_4cfc(struct TrackData *track) {
    unsigned total=track->unk_C+track->unk_E;
    track->unk_E=total&255;
    return (total>>8)&255;
}
void unk_4d1c(struct TrackData *track,struct TrackVariables *v) {
    track->unk_A=v->pRawData[1]*2; v->pRawData+=2;
    track->unk_C=track->unk_A==150 ? 256 : ((uint32_t)track->unk_A*256*0x1b4e)>>20;
    track->unk_E=0;
}
void UpdateAudio(void) {
    for (unsigned i=0;i<9;++i) if (gSoundQueue[i].exists) {
        InitTrack(sMusicTrackDataRom[i].pTrack,(const u32*)gSoundQueue[i].pHeader);
        memset(&gSoundQueue[i],0,sizeof(gSoundQueue[i]));
    }
    unsigned priority=gMusicInfo.priority;
    unsigned first=priority&0x80 ? 1 : 0;
    if ((priority&0x80) && (priority&127)==4) first=9;
    for (unsigned i=first;i<9;++i) {
        if ((priority&127)==4 && i!=(gMusicInfo.unk_20 ? 1u : 0u)) continue;
        struct TrackData *track=sMusicTrackDataRom[i].pTrack;
        if (track && track->flags&2) UpdateTrack(track);
    }
    if (!gMusicInfo.occupied) {
        gMusicInfo.occupied=1;
        if (gMusicInfo.unk_E) {
            gMusicInfo.unk_10+=gMusicInfo.unk_C;
            if (gMusicInfo.unk_10>=gMusicInfo.unk_E) gMusicInfo.unk_10-=gMusicInfo.unk_E;
        }
        UpdatePsgSounds(); UpdateMusic();
    }
    gMusicInfo.occupied=0;
}
void unk_4e10(struct TrackVariables *v) {
    const uint8_t *group=(const uint8_t*)v->pSample2;
    unsigned index=v->unk_1;
    if ((v->unk_0&0xf0)==0x80) {
        const uint8_t *raw=group+index*12;
        v->unk_35=raw[1];
        if (!raw[3]) v->unk_6=64;
        else if (raw[3]>=128) v->unk_6=raw[3]-128;
    } else {
        uint32_t map_address; memcpy(&map_address,&v->envelope2,4);
        const uint8_t *map=port_resolve_addr(map_address);
        index=map[index];
    }
    const uint8_t *raw=group+index*12;
    v->channel=raw[0]; v->unk_36=raw[2];
    unsigned channel=v->channel&7;
    uint32_t sample=raw32(raw+4);
    if (!channel) v->pSample1=port_resolve_addr(sample);
    else if (channel<3) {
        v->unk_37=(!(raw[3]&128) && raw[3]&0x70) ? raw[3] : 8;
        v->pSample1=(u32*)(uintptr_t)((sample<<6)&255);
    } else if (channel==3) UploadSampleToWaveRam(port_resolve_addr(sample));
    else if (channel==4) v->pSample1=(u32*)(uintptr_t)((sample<<3)&255);
    memcpy(&v->envelope1,raw+8,4);
}
void unk_4eb4(struct TrackVariables *v) {
    int bend=v->pitchBend;
    if (bend>0) ++bend;
    int value=bend*v->bendRange*4 + (int8_t)v->keyShift*256 + (int8_t)v->unk_1D*256 + (int8_t)v->tune*16 + v->unk_1F;
    if (v->lfoSpeed && v->modulationDepth && !v->modulationType) value+=(int8_t)v->unk_13*48;
    v->unk_17=value>>8; v->unk_18=value;
}
void unk_4f10(struct TrackVariables *v) {
    int volume=v->volume*v->unk_5>>5;
    if (v->modulationType==1) { volume=volume*((int8_t)v->unk_13+65)>>6; if (volume>255) volume=255; }
    int pan=(int8_t)v->unk_7+v->unk_6-64;
    if (v->modulationType==2) pan+=(int8_t)v->unk_13;
    if (pan>63) pan=63;
    if (pan < -64) pan=-64;
    v->unk_8=(64+pan)*volume>>7; v->unk_9=(64-pan)*volume>>7;
}
u32 CallGetNoteFrequency(u32 key,u32 multiplier) { return ((uint64_t)key*multiplier)>>18; }
void unk_4f8c(struct SoundChannel *channel,struct TrackVariables *v,s32 priority) {
    channel->unk_13|=2; unk_20a4(channel);
    if (v->pChannel) v->pChannel->pChannel1=channel;
    channel->pChannel2=v->pChannel; channel->pChannel1=NULL; channel->pVariables=v; v->pChannel=channel;
    channel->unk_0=1; channel->unk_1=v->channel; channel->unk_2=priority; channel->unk_3=v->unk_6;
    channel->envelope=v->envelope1;
    channel->unk_C=v->unk_C; channel->unk_D=v->unk_D; channel->unk_E=v->unk_E; channel->unk_F=v->unk_F;
    channel->pSample=v->pSample1; channel->pData=(uint8_t*)v->pSample1+16; channel->pSize=v->pSample1+3;
    channel->unk_6=v->unk_1; channel->unk_7=(v->unk_0&0xf0)==0x80 ? v->unk_35 : v->unk_1;
    int note=(int8_t)v->unk_17+channel->unk_7; if (note<0) note=0; if (note>127) note=127;
    uint32_t frequency=MidiKey2Freq(v->pSample1,note,v->unk_18);
    channel->unk_1C=frequency==gMusicInfo.sampleRate ? 0x4000 : CallGetNoteFrequency(frequency,gMusicInfo.pitch);
}
void AudioCommand_Goto(struct TrackVariables *v) { v->pRawData=port_resolve_addr(raw32(v->pRawData+1)); }
void AudioCommand_PatternPlay(struct TrackVariables *v) {
    const uint8_t *next=v->pRawData+5;
    v->pRawData=port_resolve_addr(raw32(v->pRawData+1));
    for (unsigned i=0;i<3;++i) if (!v->patternStartPointers[i]) { v->patternStartPointers[i]=next; break; }
}
void UploadSampleToWaveRam(const u32 *sample) {
    const void *raw=port_resolve_addr((uintptr_t)sample);
    if (raw) memcpy(gIoMem+0x90,raw,16);
}
void unk_5104(struct PsgSoundData *sound) {
    if (!sound->pVariables) return;
    unsigned channel=sound->pVariables->channel;
    uint16_t frequency=sound->unk_14;
    if (channel>8) {
        unsigned bias=gIoMem[0x89]&0xc0;
        if (bias<0x80) { frequency+=(bias<0x40 ? 2 : 1); frequency&=0xc7fe; }
    }
    unsigned base=0x60;
    switch(channel&7) {
    case 1: gIoMem[base]=sound->unk_10; base+=2; break;
    case 2: base+=8; break;
    case 3: gIoMem[base+16]=sound->unk_10; base+=18; break;
    case 4: base+=24; break;
    default: return;
    }
    gIoMem[base]=sound->unk_11; gIoMem[base+1]=sound->unk_12;
    gIoMem[base+2]=frequency; gIoMem[base+3]=frequency>>8;
    sound->unk_14=frequency&0x7fff;
}
u8 *CallSoundCodeB(u32 *left,u32 *right,u32 *mix,u8 count) {
    if (!AudioFilters_Get(AUDIO_REVERB)) { memset(mix,0,count*4u*sizeof(*mix)); return (u8*)(mix+count*4u); }
    const int8_t *l=(const int8_t*)left, *r=(const int8_t*)right;
    for (unsigned i=0;i<count*2u;++i) {
        int value=(l[i]+r[i]+l[i+PCM_DMA_BUF_SIZE]+r[i+PCM_DMA_BUF_SIZE])*gMusicInfo.reverb;
        value=value/512*128;
        mix[i*2]=value; mix[i*2+1]=value;
    }
    return (u8*)(mix+count*4u);
}
void CallSoundCodeA(struct SoundChannel *channel,u32 *mix,u8 count) {
    if (!channel->pSample || !count) return;
    uint32_t length=channel->pSample[3], loop_start=channel->pSample[2];
    if (!length || length>0x800000 || loop_start>=length) loop_start=0;
    if (!length || length>0x800000) { channel->unk_0=0; return; }
    const int8_t *data=(const int8_t*)channel->pData;
    uint64_t position=channel->unk_18;
    bool direct=channel->unk_1==8, reverse=channel->unk_1==0x20;
    uint64_t step=direct ? 1 : channel->unk_1C;
    bool looping=(((const uint8_t*)channel->pSample)[2] | ((const uint8_t*)channel->pSample)[3])!=0;
    for (unsigned i=0;i<count*4u;++i) {
        if (reverse) {
            uint64_t start=(uint64_t)loop_start<<14;
            if (position<step || position-step<start) {
                if (!looping) { channel->unk_0=0; break; }
                uint64_t range=(uint64_t)(length-loop_start)<<14;
                uint64_t deficit=start+step-position;
                position=((uint64_t)length<<14)-(deficit%range);
                if (position==((uint64_t)length<<14)) --position;
            } else position-=step;
        }
        uint32_t index=direct ? position : position>>14;
        if (index>=length) {
            if (!looping) { channel->unk_0=0; break; }
            index=loop_start+(index-loop_start)%(length-loop_start);
            position=direct ? index : ((uint64_t)index<<14)|(position&0x3fff);
        }
        int sample=data[index];
        if (!direct && index+1<length) sample+=((data[index+1]-sample)*(int)(position&0x3fff))>>14;
        mix[i*2]+=(int32_t)(sample*channel->unk_11);
        mix[i*2+1]+=(int32_t)(sample*channel->unk_12);
        if (!reverse) position+=step;
    }
    channel->unk_18=position;
}
u8 *CallSoundCodeC(u32 *destination,u32 *mix,u8 count) {
    uint8_t *left=(uint8_t*)destination, *right=left+PCM_DMA_BUF_SIZE;
    static int16_t pcm[1024];
    unsigned n=count*4u;
    if (n>512) return (u8*)mix;
    PortPsg_BeginBlock(gMusicInfo.sampleRate);
    bool enable_psg=AudioFilters_Get(AUDIO_PSG);
    /* Fixed mixer headroom; master gain belongs to the final output stage. */
    unsigned volume=100;
    unsigned peak=0,clipped=0;
    for (unsigned i=0;i<n;++i) {
        int32_t raw_l=(int32_t)mix[i*2], raw_r=(int32_t)mix[i*2+1];
        int l8=raw_l>>7, r8=raw_r>>7;
        if (l8>127) l8=127;
        if (l8 < -128) l8=-128;
        if (r8>127) r8=127;
        if (r8 < -128) r8=-128;
        left[i]=(uint8_t)l8; right[i]=(uint8_t)r8;
        int psg_l,psg_r; PortPsg_NextSample(&psg_l,&psg_r);
        if (!enable_psg) psg_l=psg_r=0;
        pcm[i*2]=AudioVolume_Mix(raw_l,psg_l,volume);
        pcm[i*2+1]=AudioVolume_Mix(raw_r,psg_r,volume);
        for (unsigned side=0;side<2;++side) {
            int sample=pcm[i*2+side]; unsigned magnitude=sample<0 ? -sample : sample;
            if (magnitude>peak) peak=magnitude;
            if (sample==32767 || sample==-32768) ++clipped;
        }
    }
    AudioSwitch_Submit(pcm,n,gMusicInfo.sampleRate);
    Diagnostics_Audio(n,gMusicInfo.sampleRate,gMusicInfo.currentSoundChannel,peak,clipped);
    return (u8*)(mix+n*2);
}
void sub_08004ad8(void) {
    if (++gMusicInfo.unk_10>=gMusicInfo.unk_E) gMusicInfo.unk_10=0;
}
