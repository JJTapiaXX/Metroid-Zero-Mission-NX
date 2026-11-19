/* SPDX-License-Identifier: GPL-3.0-or-later */
#define PLATFORM_SWITCH2 1
#include "../../switch/source/diagnostics.c"
#include <arm_neon.h>

static unsigned lock_depth,lock_error,io_count,flush_count,inject;
void *memcpy(void *dst,const void *src,size_t count) {
    unsigned char *d=dst;const unsigned char *s=src;
    while(count>=16){vst1q_u8(d,vld1q_u8(s));d+=16;s+=16;count-=16;}
    while(count--) *d++=*s++;
    return dst;
}
size_t strlen(const char *s){size_t n=0;while(s[n])++n;return n;}
int snprintf(char *s,size_t n,const char *fmt,...) {
    (void)fmt;const char text[]="test record\n";
    if(n<sizeof(text))return 0;
    memcpy(s,text,sizeof(text));return sizeof(text)-1;
}
void mutexLock(Mutex *m){(void)m;if(lock_depth)++lock_error;++lock_depth;}
void mutexUnlock(Mutex *m){(void)m;if(lock_depth!=1)++lock_error;--lock_depth;}
void svcSignalProcessWideKey(u32 *key,s32 count){(void)key;(void)count;}
Result condvarWaitTimeout(CondVar *c,Mutex *m,u64 timeout){(void)c;(void)m;(void)timeout;return 0;}
int fprintf(FILE *f,const char *fmt,...){(void)f;(void)fmt;if(lock_depth)++lock_error;++io_count;return 0;}
size_t fwrite(const void *data,size_t size,size_t count,FILE *f){
    (void)data;(void)size;(void)f;if(lock_depth)++lock_error;++io_count;
    /* A producer can continue while the writer is in a slow SD operation. */
    if(inject){inject=0;Diagnostics_Event("producer while SD is busy");}
    return count;
}
int fflush(FILE *f){(void)f;if(lock_depth)++lock_error;++flush_count;return 0;}

int overlay_test(void){
    runtime=(FILE*)1;log_started=true;log_running=false;
    for(unsigned i=0;i<LOG_RECORD_COUNT+5;++i)Diagnostics_Event("saturate queue");
    if(io_count || flush_count)return 1;
    if(log_count!=LOG_RECORD_COUNT || log_dropped!=5)return 2;
    inject=1;log_writer(NULL);
    if(log_count || log_dropped || lock_depth || lock_error)return 3;
    if(flush_count!=LOG_RECORD_COUNT+1 || io_count!=LOG_RECORD_COUNT+2)return 4;
    unsigned before=io_count;
    unsigned expected_head=(log_head+3)%LOG_RECORD_COUNT;
    for(unsigned i=0;i<3;++i)Diagnostics_Event("wraparound");
    if(io_count!=before)return 5;
    log_writer(NULL);
    if(log_head!=expected_head || log_count || lock_error)return 6;
    log_started=false;Diagnostics_Event("logging unavailable");
    if(io_count!=before+3 || log_count)return 7;
    return 0;
}
