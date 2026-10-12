/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include "diagnostics.h"
#include "audio_filters.h"
#include "screen_overlay.h"

/* Exception-path I/O uses its own pre-opened file session and no stdio,
 * allocator, or game mutex. OS process termination may still bypass this. */
static FsFile crash_file;
static FsFileSystem crash_fs;
static bool crash_ready;
static FILE *runtime;
static unsigned frame_count;
static atomic_uint logic_us,ppu_us,ui_us,present_us;
void Diagnostics_Performance(unsigned logic,unsigned ppu,unsigned ui,unsigned present) {
    atomic_store(&logic_us,logic); atomic_store(&ppu_us,ppu);
    atomic_store(&ui_us,ui); atomic_store(&present_us,present);
}
static atomic_uint mode_value,submode_value,area_value,room_value,pose_value,x_value,y_value,phase_value;
static atomic_uint audio_frames,audio_rate,audio_channels,audio_peak,audio_clipped;
static atomic_uint queue_depth,dropped_blocks,starved_blocks,audio_result;
static atomic_uint output_volume,output_gain,output_peak,output_rc,output_hardware,output_actual;
static unsigned last_area=~0u,last_room=~0u,last_mode=~0u;
static char crash_buffer[8192];
__attribute__((aligned(16))) u8 __nx_exception_stack[0x10000];
u64 __nx_exception_stack_size=sizeof(__nx_exception_stack);
extern void _start(void);

void Diagnostics_Init(void) {
    mkdir("logs",0777);
    remove("logs/runtime-previous.log");
    rename("logs/runtime.log","logs/runtime-previous.log");
    runtime=fopen("logs/runtime.log","wb");
    if (runtime) {
        fprintf(runtime,"Metroid: Zero Mission NX 0.1 fix20\nimage_base=%p\n",_start);
        fflush(runtime);
    }
    if (R_SUCCEEDED(fsOpenSdCardFileSystem(&crash_fs))) {
        const char *path="/switch/mzm/logs/crash-last.txt";
        fsFsCreateFile(&crash_fs,path,sizeof(crash_buffer),0);
        crash_ready=R_SUCCEEDED(fsFsOpenFile(&crash_fs,path,FsOpenMode_Write,&crash_file));
        if (crash_ready) fsFileSetSize(&crash_file,sizeof(crash_buffer));
    }
}
void Diagnostics_Event(const char *message) {
    if (runtime) { fprintf(runtime,"event frame=%u %s\n",frame_count,message); fflush(runtime); }
}
void Diagnostics_Checkpoint(unsigned phase) { atomic_store(&phase_value,phase); }
void Diagnostics_Audio(unsigned frames,unsigned rate,unsigned channels,unsigned peak,unsigned clipped) {
    atomic_fetch_add(&audio_frames,frames); atomic_store(&audio_rate,rate);
    atomic_store(&audio_channels,channels); atomic_store(&audio_peak,peak);
    atomic_fetch_add(&audio_clipped,clipped);
}
void Diagnostics_AudioQueue(unsigned queued,unsigned dropped,unsigned starved,uint32_t result) {
    atomic_store(&queue_depth,queued); atomic_store(&dropped_blocks,dropped);
    atomic_store(&starved_blocks,starved); atomic_store(&audio_result,result);
}
void Diagnostics_Output(unsigned percent,unsigned gain,float actual,uint32_t result,bool hardware,unsigned peak) {
    atomic_store(&output_volume,percent); atomic_store(&output_gain,gain);
    atomic_store(&output_hardware,hardware); atomic_store(&output_peak,peak);
    if (actual>=0.0f) atomic_store(&output_actual,(unsigned)(actual*1000000.0f));
    if (result) atomic_store(&output_rc,result);
}
void Diagnostics_Frame(unsigned mode,unsigned submode,unsigned area,unsigned room,unsigned pose,unsigned x,unsigned y,bool overlay) {
    ++frame_count;
    atomic_store(&mode_value,mode); atomic_store(&submode_value,submode);
    atomic_store(&area_value,area); atomic_store(&room_value,room); atomic_store(&pose_value,pose);
    atomic_store(&x_value,x); atomic_store(&y_value,y);
    bool transition=area!=last_area || room!=last_room || mode!=last_mode;
    if (runtime && (transition || frame_count%60==0)) {
        fprintf(runtime,"frame=%u mode=%u sub=%u area=%u room=%u pose=%u xy=%u,%u overlay=%u phase=%u\n",
            frame_count,mode,submode,area,room,pose,x,y,(unsigned)overlay,atomic_load(&phase_value));
        u64 used=0,total=0;
        svcGetInfo(&used,InfoType_UsedMemorySize,CUR_PROCESS_HANDLE,0);
        svcGetInfo(&total,InfoType_TotalMemorySize,CUR_PROCESS_HANDLE,0);
        fprintf(runtime,"memory used=%llu total=%llu\n",(unsigned long long)used,(unsigned long long)total);
        fprintf(runtime,"display layout=%u scaling=%u fps=%u\n",SwitchOverlay_Layout(),SwitchOverlay_Scaling(),(unsigned)SwitchOverlay_FpsVisible());
        fprintf(runtime,"performance logic_us=%u ppu_us=%u ui_us=%u present_us=%u\n",
            atomic_load(&logic_us),atomic_load(&ppu_us),atomic_load(&ui_us),atomic_load(&present_us));
        struct mallinfo heap=mallinfo();
        fprintf(runtime,"heap allocated=%llu free=%llu arena=%llu\n",
            (unsigned long long)heap.uordblks,(unsigned long long)heap.fordblks,(unsigned long long)heap.arena);
        fflush(runtime);
    }
    last_area=area; last_room=room; last_mode=mode;
}
void Diagnostics_Close(void) {
    Diagnostics_Event("clean shutdown");
    if (runtime) fclose(runtime);
    runtime=NULL;
    if (crash_ready) fsFileClose(&crash_file);
    if (serviceIsActive(&crash_fs.s)) fsFsClose(&crash_fs);
    crash_ready=false;
}
static size_t append_text(size_t pos,const char *s) {
    while (*s && pos<sizeof(crash_buffer)-1) crash_buffer[pos++]=*s++;
    return pos;
}
static size_t append_hex(size_t pos,uint64_t value) {
    for (int shift=60;shift>=0;shift-=4)
        if (pos<sizeof(crash_buffer)-1) crash_buffer[pos++]="0123456789abcdef"[(value>>shift)&15];
    return pos;
}
static size_t record(size_t pos,const char *name,uint64_t value) {
    pos=append_text(pos,name); pos=append_hex(pos,value); return append_text(pos,"\n");
}
void __libnx_exception_handler(ThreadExceptionDump *ctx) {
    size_t pos=append_text(0,"Metroid: Zero Mission NX fix20 exception\n");
    pos=record(pos,"image_base=",(uintptr_t)_start);
    pos=record(pos,"error=",ctx->error_desc); pos=record(pos,"pc=",ctx->pc.x);
    pos=record(pos,"pc_offset=",ctx->pc.x-(uintptr_t)_start);
    pos=record(pos,"lr=",ctx->lr.x); pos=record(pos,"sp=",ctx->sp.x);
    pos=record(pos,"lr_offset=",ctx->lr.x-(uintptr_t)_start);
    pos=record(pos,"far=",ctx->far.x); pos=record(pos,"esr=",ctx->esr);
    pos=record(pos,"phase=",atomic_load(&phase_value)); pos=record(pos,"mode=",atomic_load(&mode_value));
    pos=record(pos,"area=",atomic_load(&area_value)); pos=record(pos,"room=",atomic_load(&room_value));
    pos=record(pos,"pose=",atomic_load(&pose_value)); pos=record(pos,"x=",atomic_load(&x_value));
    pos=record(pos,"y=",atomic_load(&y_value));
    for (unsigned i=0;i<29;++i) {
        pos=append_text(pos,"x");
        if (i>=10) crash_buffer[pos++]='0'+i/10;
        crash_buffer[pos++]='0'+i%10; pos=append_text(pos,"=");
        pos=append_hex(pos,ctx->cpu_gprs[i].x); pos=append_text(pos,"\n");
    }
    if (crash_ready) {
        fsFileWrite(&crash_file,0,crash_buffer,pos,FsWriteOption_Flush);
        fsFileSetSize(&crash_file,pos);
        fsFileFlush(&crash_file);
    }
    svcExitProcess();
}
