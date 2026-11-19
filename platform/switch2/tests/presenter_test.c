/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Include the real presenter so the test can inject CPU/GPU allocations without
 * starting a GPU driver. Vulkan calls are replaced by no-op submission stubs. */
#include "../source/presenter_ounce.c"
#include <arm_neon.h>

uint32_t test_canvas[SWITCH_SCREEN_WIDTH*SWITCH_SCREEN_HEIGHT];
uint32_t test_upload[SWITCH_SCREEN_WIDTH*SWITCH_SCREEN_HEIGHT];
static uint32_t main_pixels[320*192],second_pixels[320*240];
volatile unsigned cpu_phase;
static unsigned waits,submissions,presents;
static unsigned startup_services;
bool appletMainLoop(void) { return true; }
__attribute__((noinline)) void svcSleepThread(s64 ns) { __asm__ volatile(""::"r"(ns):"memory"); }
void Achievements_Update(void) { ++startup_services; }

void *memcpy(void *dst,const void *src,size_t count) {
    unsigned char *d=dst; const unsigned char *s=src;
    while(count>=16) { vst1q_u8(d,vld1q_u8(s)); d+=16;s+=16;count-=16; }
    while(count--) *d++=*s++;
    return dst;
}
void *memset(void *dst,int value,size_t count) {
    unsigned char *d=dst; while(count--) *d++=(unsigned char)value; return dst;
}
void *calloc(size_t count,size_t size) { (void)count;(void)size;return NULL; }
void free(void *ptr) { (void)ptr; }
int snprintf(char *s,size_t n,const char *fmt,...) { (void)s;(void)n;(void)fmt;return 0; }
void Diagnostics_Event(const char *message) { (void)message; }

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice d,uint32_t n,const VkFence *f,VkBool32 all,uint64_t timeout) {
    (void)d;(void)n;(void)f;(void)all;(void)timeout;++waits;return VK_SUCCESS;
}
VKAPI_ATTR __attribute__((noinline)) VkResult VKAPI_CALL vkQueueSubmit(VkQueue q,uint32_t n,const VkSubmitInfo *s,VkFence f) {
    (void)q;(void)f;
    if(n!=1 || s->commandBufferCount!=1 || s->signalSemaphoreCount!=1) return VK_ERROR_UNKNOWN;
    ++submissions;return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue q,const VkPresentInfoKHR *p) {
    (void)q;if(p->swapchainCount!=1 || p->waitSemaphoreCount!=1) return VK_ERROR_UNKNOWN;
    ++presents;return VK_SUCCESS;
}

int overlay_test(void) {
    static VkImage image=(VkImage)1;
    static VkSemaphore semaphore=(VkSemaphore)2;
    r.device=(VkDevice)1;r.fence=(VkFence)1;r.queue=(VkQueue)1;
    r.swapchain=(VkSwapchainKHR)1;r.cmd=(VkCommandBuffer)1;
    r.images=&image;r.rendered=&semaphore;r.count=1;
    r.extent=(VkExtent2D){1280,720};r.coherent=true;
    r.canvas=test_canvas;r.pixels=test_upload;
    SwitchSurface main={main_pixels,320,192,320},second={second_pixels,320,240,320};
    for(unsigned i=0;i<320*192;++i)main_pixels[i]=0xff000000u|(i*37u&0xffffffu);
    for(unsigned i=0;i<320*240;++i)second_pixels[i]=0xff000000u|(i*71u&0xffffffu);
    for(unsigned layout=0;layout<6;++layout) {
        SwitchOverlay_SetOptions(layout,4);
        SwitchOverlay_SetLcd(layout%4);
        SwitchOverlay overlay={.visible=layout==0};
        uint32_t stride=0;
        uint32_t *output=PresenterOunce_Begin(&stride);
        if(output!=test_canvas || output==test_upload || stride!=1280*4) return 1;
        if(PresenterOunce_Begin(&stride)!=NULL) return 2;
        cpu_phase=1;
        if(!SwitchOverlay_Compose(&overlay,output,stride/4,&main,&second)) return 3;
        cpu_phase=0;
        if(!PresenterOunce_End()) return 4;
        if(PresenterOunce_End()) return 5;
    }
    if(waits!=6 || submissions!=6 || presents!=6) return 6;
    if(!r.texture_initialized || r.begun) return 7;
    /* Exercise the explicit flush path as well as coherent staging. */
    r.coherent=false;
    uint32_t stride;
    if(PresenterOunce_Begin(&stride)!=test_canvas || !PresenterOunce_End()) return 8;
    if(!PresenterOunce_Warmup(20) || startup_services<2) return 9;
    return 0;
}
