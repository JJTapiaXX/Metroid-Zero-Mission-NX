/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Minimal libnx test double. Production builds use the installed libnx header. */
#ifndef MZM_TEST_SWITCH_H
#define MZM_TEST_SWITCH_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint32_t u32;
typedef uint32_t Result;
typedef struct { int unused; } Thread;
typedef uint32_t Mutex;
static inline void mutexInit(Mutex *m) { *m=0; }
void mutexLock(Mutex *m);
void mutexUnlock(Mutex *m);
Result threadCreate(Thread *t,void (*entry)(void*),void *arg,void *stack,size_t size,int priority,int core);
Result threadStart(Thread *t);
Result threadWaitForExit(Thread *t);
Result threadClose(Thread *t);
typedef struct { int unused; } Framebuffer;
typedef struct { int unused; } PadState;
typedef struct { uint32_t x, y; } HidTouchState;
typedef struct { int32_t count; HidTouchState touches[16]; } HidTouchScreenState;
#define R_FAILED(result) ((result) != 0)
#define PIXEL_FORMAT_RGBA_8888 1
#define HidNpadStyleSet_NpadStandard 1
#define HidNpadButton_A (1ull << 0)
#define HidNpadButton_B (1ull << 1)
#define HidNpadButton_StickR (1ull << 5)
#define HidNpadButton_L (1ull << 6)
#define HidNpadButton_R (1ull << 7)
#define HidNpadButton_ZL (1ull << 8)
#define HidNpadButton_ZR (1ull << 9)
#define HidNpadButton_Plus (1ull << 10)
#define HidNpadButton_Minus (1ull << 11)
#define HidNpadButton_AnyLeft (1ull << 12)
#define HidNpadButton_AnyUp (1ull << 13)
#define HidNpadButton_AnyRight (1ull << 14)
#define HidNpadButton_AnyDown (1ull << 15)
void *nwindowGetDefault(void);
Result framebufferCreate(Framebuffer *, void *, int, int, int, int);
Result framebufferMakeLinear(Framebuffer *);
void framebufferClose(Framebuffer *);
void *framebufferBegin(Framebuffer *, u32 *);
void framebufferEnd(Framebuffer *);
void padConfigureInput(int, int);
void padInitializeDefault(PadState *);
void padUpdate(PadState *);
uint64_t padGetButtons(PadState *);
uint64_t padGetButtonsDown(PadState *);
void hidInitializeTouchScreen(void);
size_t hidGetTouchScreenStates(HidTouchScreenState *, size_t);
bool appletMainLoop(void);
uint64_t armGetSystemTick(void);
uint64_t armTicksToNs(uint64_t ticks);
void svcSleepThread(int64_t ns);
typedef enum { PcmFormat_Invalid, PcmFormat_Int16 } PcmFormat;
typedef struct AudioOutBuffer {
    struct AudioOutBuffer *next;
    void *buffer;
    uint64_t buffer_size, data_size, data_offset;
} AudioOutBuffer;
#define R_SUCCEEDED(result) ((result)==0)
void armDCacheFlush(void *addr,size_t size);
Result audoutSetAudioOutVolume(float volume);
Result audoutGetAudioOutVolume(float *volume);
Result audoutInitialize(void);
void audoutExit(void);
u32 audoutGetSampleRate(void);
u32 audoutGetChannelCount(void);
PcmFormat audoutGetPcmFormat(void);
Result audoutStartAudioOut(void);
Result audoutStopAudioOut(void);
Result audoutGetReleasedAudioOutBuffer(AudioOutBuffer **buffer,u32 *count);
Result audoutAppendAudioOutBuffer(AudioOutBuffer *buffer);
#endif
