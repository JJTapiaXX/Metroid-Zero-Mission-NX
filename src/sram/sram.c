#include "sram/sram.h"

#include "gba.h"
#include "io.h"

static const char sSramVersion[] = "SRAM_V113";

#if defined(MZM_3DS) || defined(PORT_NATIVE)
#include <string.h>
#include <stdio.h>
#include "port_gba_mem.h"
#ifdef MZM_3DS
#include "port_debug_log.h"
#endif

#define SAVE_FILE_PATH "mzm.sav"

/* Writes to SD are far too slow to run once per SramWrite* call (a single
 * in-game save fans out into dozens of them via unk_fbc / SramSaveFile's
 * staged state machine, and each one used to rewrite the whole 64 KB file
 * synchronously -- the source of the save-point frame hitches, issue #22).
 * Every write only touches gSramMem and marks it dirty. Port_FlushSramIfDirty()
 * runs once per frame and hands a snapshot of gSramMem to a dedicated save
 * thread that does the SD write in the background.
 *
 * The game thread never waits for the SD. The save machine writes one 0x200
 * byte chunk a frame for a dozen frames (unk_fbc), and a 64 KB write + remove
 * + rename takes longer than a frame, so a flush that waited for the previous
 * write to finish held the game at the SD's pace (25-30 fps while saving).
 * Two things prevent that: the flush waits for the writes to stop (a few
 * frames without one, or a cap, so a save becomes one file write instead of
 * one a frame), and there are two snapshot buffers, one being written and one
 * queued, the queued one simply replaced by a newer snapshot.
 * Port_FlushSramWait() blocks until everything is on disk (shutdown path). */
#ifdef MZM_3DS
#include <pthread.h>

/* Frames without an SramWrite* before the file is written, and the most a
 * steady stream of them may put the write off. */
#define SRAM_FLUSH_IDLE_FRAMES 3
#define SRAM_FLUSH_MAX_FRAMES 90

static u8 sSramSnapshot[2][0x10000];   /* same size as gSramMem */
static u8 sSramDirty = FALSE;          /* set by SramWrite*, game thread */
static u8 sDirtyIdleFrames;            /* frames since the last SramWrite*, game thread */
static u8 sDirtyAgeFrames;             /* frames since the file was last behind, game thread */
static s8 sQueuedBuffer = -1;          /* snapshot waiting for the thread, under sSaveMutex */
static s8 sWritingBuffer = -1;         /* snapshot the thread is writing, under sSaveMutex */
static u8 sShutdown = FALSE;
static pthread_mutex_t sSaveMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sSaveCond = PTHREAD_COND_INITIALIZER;
static pthread_t sSaveThread;
static u8 sSaveThreadStarted = FALSE;

static u8 SramWriteAtomic(const u8* data, u32 size);
static volatile u8 sWriteFailed; /* set by the save thread, shown by the game thread */
extern void Port_SaveFile_ShowFailed(void);

static void* SaveThreadMain(void* arg)
{
    (void)arg;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    for (;;)
    {
        pthread_mutex_lock(&sSaveMutex);
        while (sQueuedBuffer < 0 && !sShutdown)
            pthread_cond_wait(&sSaveCond, &sSaveMutex);
        if (sQueuedBuffer < 0 && sShutdown)
        {
            pthread_mutex_unlock(&sSaveMutex);
            break;
        }
        /* Take the queued snapshot: from here the producer may queue a newer
         * one in the other buffer, and never touches this one until the write
         * below is over. */
        sWritingBuffer = sQueuedBuffer;
        sQueuedBuffer = -1;
        pthread_mutex_unlock(&sSaveMutex);

        if (SramWriteAtomic(sSramSnapshot[sWritingBuffer], sizeof(sSramSnapshot[0])))
            Port_DebugLog("Port_SaveSram: wrote mzm.sav");
        else
        {
            Port_DebugLog("Port_SaveSram: FAILED to write mzm.sav");
            sWriteFailed = TRUE;
        }

        pthread_mutex_lock(&sSaveMutex);
        sWritingBuffer = -1;
        pthread_cond_broadcast(&sSaveCond);
        pthread_mutex_unlock(&sSaveMutex);
    }
    return NULL;
}

static void SaveThreadStart(void)
{
    if (!sSaveThreadStarted)
    {
        sSaveThreadStarted = TRUE;
        if (pthread_create(&sSaveThread, NULL, SaveThreadMain, NULL) != 0)
            sSaveThreadStarted = FALSE;
    }
}
#endif

/* A whole file only: a save cut short by a power loss is smaller than gSramMem. */
static int ReadSramFile(const char* path)
{
    long size;
    FILE* f = fopen(path, "rb");
    if (!f)
        return FALSE;
    size = -1;
    if (fseek(f, 0, SEEK_END) == 0)
        size = ftell(f);
    if (size != (long)sizeof(gSramMem) || fseek(f, 0, SEEK_SET) != 0
        || fread(gSramMem, 1, sizeof(gSramMem), f) != sizeof(gSramMem))
    {
        fclose(f);
        return FALSE;
    }
    fclose(f);
    return TRUE;
}

void Port_LoadSram(void)
{
    /* mzm.sav first; if it is missing or incomplete, a complete .tmp (the
     * crash came between the two renames of SramWriteAtomic), then the save
     * before it (.bak). None of them: a blank file, as a first run. */
    if (ReadSramFile(SAVE_FILE_PATH))
        return;
    if (ReadSramFile(SAVE_FILE_PATH ".tmp"))
        return;
    ReadSramFile(SAVE_FILE_PATH ".bak");
}

/* Atomic save write: the data lands in a temp file first and only replaces
 * mzm.sav via rename once fully written. A power loss mid-write can then
 * never leave a truncated/zeroed mzm.sav (issue #22 follow-up: a "wb" open
 * truncates immediately, so dying mid-fwrite looked like a wiped save).
 * The save before is kept as mzm.sav.bak, and Port_LoadSram falls back to
 * .tmp and .bak. Returns FALSE when the file could not be written. */
static u8 SramWriteAtomic(const u8* data, u32 size)
{
    u8 ok;
    FILE* f = fopen(SAVE_FILE_PATH ".tmp", "wb");
    if (!f)
        return FALSE;
    ok = fwrite(data, 1, size, f) == size;
    ok = (fflush(f) == 0) && ok;
    ok = (fclose(f) == 0) && ok;
    if (!ok)
    {
        remove(SAVE_FILE_PATH ".tmp");
        return FALSE;
    }
    /* FatFs rename fails if the destination exists -- swap by hand. Between
     * the renames a complete .tmp and a complete .bak are on the card. */
    remove(SAVE_FILE_PATH ".bak");
    rename(SAVE_FILE_PATH, SAVE_FILE_PATH ".bak"); /* absent on a first save */
    return rename(SAVE_FILE_PATH ".tmp", SAVE_FILE_PATH) == 0;
}

void Port_SaveSram(void)
{
    u8 ok = SramWriteAtomic(gSramMem, sizeof(gSramMem));
#ifdef MZM_3DS
    if (!ok)
        sWriteFailed = TRUE;
    Port_DebugLog(ok ? "Port_SaveSram: wrote mzm.sav" : "Port_SaveSram: FAILED to write mzm.sav");
#else
    (void)ok;
#endif
}

#ifdef MZM_3DS
/* Copies gSramMem into the buffer the thread is not writing and queues it,
 * replacing a snapshot still waiting. Never waits for the SD. */
static void QueueSnapshot(void)
{
    pthread_mutex_lock(&sSaveMutex);
    {
        const s8 buffer = sWritingBuffer == 0 ? 1 : 0;
        memcpy(sSramSnapshot[buffer], gSramMem, sizeof(gSramMem));
        sQueuedBuffer = buffer;
    }
    pthread_cond_signal(&sSaveCond);
    pthread_mutex_unlock(&sSaveMutex);
}
#endif

void Port_FlushSramIfDirty(void)
{
#ifdef MZM_3DS
    if (sWriteFailed)
    {
        sWriteFailed = FALSE;
        Port_SaveFile_ShowFailed();
    }
#endif
    if (!sSramDirty)
        return;
#ifdef MZM_3DS
    if (sSaveThreadStarted || (SaveThreadStart(), sSaveThreadStarted))
    {
        /* Wait for the stream of writes to stop (or for the cap), so a save
         * that writes a chunk a frame is one file write, not twelve. */
        if (sDirtyIdleFrames < 255) sDirtyIdleFrames++;
        if (sDirtyAgeFrames < 255) sDirtyAgeFrames++;
        if (sDirtyIdleFrames < SRAM_FLUSH_IDLE_FRAMES && sDirtyAgeFrames < SRAM_FLUSH_MAX_FRAMES)
            return;
        sSramDirty = FALSE;
        sDirtyIdleFrames = 0;
        sDirtyAgeFrames = 0;
        QueueSnapshot();
        /* Piggyback diagnostics on the (rare) SD write instead of per frame. */
        Port_DebugLogFlush();
        return;
    }
    sSramDirty = FALSE;
#else
    sSramDirty = FALSE;
#endif
    Port_SaveSram();
}

void Port_FlushSramWait(void)
{
#ifdef MZM_3DS
    if (sSramDirty)
    {
        /* Shutdown: no waiting for the stream of writes to stop. */
        sSramDirty = FALSE;
        sDirtyIdleFrames = sDirtyAgeFrames = 0;
        if (sSaveThreadStarted || (SaveThreadStart(), sSaveThreadStarted))
            QueueSnapshot();
        else
            Port_SaveSram();
    }
    if (!sSaveThreadStarted)
        return;
    pthread_mutex_lock(&sSaveMutex);
    while (sQueuedBuffer >= 0 || sWritingBuffer >= 0)
        pthread_cond_wait(&sSaveCond, &sSaveMutex);
    pthread_mutex_unlock(&sSaveMutex);
    /* Make sure diagnostics from this session reach the SD before exit. */
    Port_DebugLogFlush();
#else
    if (sSramDirty)
    {
        sSramDirty = FALSE;
        Port_SaveSram();
    }
#endif
}

void SramWriteUnchecked(u8* src, u8* dest, u32 size)
{
    void* d = port_resolve_write_addr((uintptr_t)dest);
    const void* s = port_resolve_copy_src(src, size);
    if (d && s) memcpy(d, s, size);
    sSramDirty = TRUE;
#ifdef MZM_3DS
    sDirtyIdleFrames = 0;
#endif
}

void SramWrite(u8* src, u8* dest, u32 size)
{
    void* d = port_resolve_write_addr((uintptr_t)dest);
    const void* s = port_resolve_copy_src(src, size);
    if (d && s) memcpy(d, s, size);
    sSramDirty = TRUE;
#ifdef MZM_3DS
    sDirtyIdleFrames = 0;
#endif
}

u8* SramCheck(u8* src, u8* dest, u32 size)
{
    u32 i;
    void* d = port_resolve_write_addr((uintptr_t)dest);
    const void* s = port_resolve_copy_src(src, size);
    if (d && s) {
        u8* d8 = (u8*)d;
        const u8* s8 = (const u8*)s;
        for (i = 0; i < size; i++) {
            if (d8[i] != s8[i]) return dest + i;
        }
    }
    return NULL;
}

u8* SramWriteChecked(u8* src, u8* dest, u32 size)
{
    SramWrite(src, dest, size);
    return SramCheck(src, dest, size);
}


#else

static void SramWriteUncheckedInternal(u8* src, u8* dest, u32 size)
{
    while (size-- != 0)
        *dest++ = *src++;
}

void SramWriteUnchecked(u8* src, u8* dest, u32 size)
{
    u16 code[0x40];
    u16* code_ptr;
    u16* func_ptr;
    u16 csize;
    void* (*func)(u8*, u8*, u32);

    WRITE_16(REG_WAITCNT, READ_16(REG_WAITCNT) & ~WAIT_SRAM_CYCLES_MASK | WAIT_SRAM_8CYCLES);

    func_ptr = (u16*)SramWriteUncheckedInternal;
    func_ptr = (u16*)((u32)func_ptr & ~1);
    code_ptr = code;

    for (csize = ((u32)SramWriteUnchecked - (u32)SramWriteUncheckedInternal) / 2; csize > 0; --csize)
        *code_ptr++ = *func_ptr++;

    func = (void* (*)(u8*, u8*, u32))code + 1;
    func(src, dest, size);
}

void SramWrite(u8* src, u8* dest, u32 size)
{
    u16 w = READ_16(REG_WAITCNT) & ~WAIT_SRAM_CYCLES_MASK | WAIT_SRAM_8CYCLES;
    WRITE_16(REG_WAITCNT, w);

    while (size-- != 0)
        *dest++ = *src++;
}

static u8* SramCheckInternal(u8* src, u8* dest, u32 size)
{
    while (size-- != 0)
    {
        if (*dest++ != *src++)
            return dest - 1;
    }

    return NULL;
}

u8* SramCheck(u8* src, u8* dest, u32 size)
{
    u16 code[0x60];
    u16* code_ptr;
    u16* func_ptr;
    u16 csize;
    void* (*func)(u8*, u8*, u32);

    WRITE_16(REG_WAITCNT, READ_16(REG_WAITCNT) & ~WAIT_SRAM_CYCLES_MASK | WAIT_SRAM_8CYCLES);

    func_ptr = (u16*)SramCheckInternal;
    func_ptr = (u16*)((u32)func_ptr & ~1);
    code_ptr = code;

    for (csize = ((u32)SramCheck - (u32)SramCheckInternal) / 2; csize > 0; --csize)
        *code_ptr++ = *func_ptr++;

    func = (void* (*)(u8*, u8*, u32))code + 1;
    return func(src, dest, size);
}

u8* SramWriteChecked(u8* src, u8* dest, u32 size)
{
    u8* diff;
    u8 i;

#ifdef BUGFIX
    diff = NULL;
#endif // BUGFIX

    for (i = 0; i < 3; ++i)
    {
        SramWrite(src, dest, size);
        diff = SramCheck(src, dest, size);
        if (!diff)
            break;
    }

    return diff;
}
#endif

