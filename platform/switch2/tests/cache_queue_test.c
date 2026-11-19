/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../source/cache_ounce.c"
#include <stdarg.h>
static unsigned locks,errors,io,writes,critical_writes,flush_failures,sleeps,creates,joins;
static bool inject;
static char opened[300];
static unsigned char newest[32];
void *memcpy(void *dst,const void *src,size_t n){unsigned char *d=dst;const unsigned char *s=src;while(n--)*d++=*s++;return dst;}
void *memmove(void *dst,const void *src,size_t n){unsigned char *d=dst;const unsigned char *s=src;if(d<s){for(size_t i=0;i<n;++i)d[i]=s[i];}else{while(n--)d[n]=s[n];}return dst;}
size_t strlen(const char *s){size_t n=0;while(s[n])++n;return n;}
int strcmp(const char *a,const char *b){while(*a && *a==*b){++a;++b;}return (unsigned char)*a-(unsigned char)*b;}
int snprintf(char *out,size_t size,const char *format,...){
    va_list args;va_start(args,format);const char *s=va_arg(args,const char*);va_end(args);
    size_t n=strlen(s),tail=strlen(format+2);
    if(n+tail>=size)return 0;
    memcpy(out,s,n);memcpy(out+n,format+2,tail+1);return n+tail;
}
void Diagnostics_Event(const char *s){(void)s;}
void mutexLock(Mutex *m){(void)m;if(locks)++errors;++locks;}
void mutexUnlock(Mutex *m){(void)m;if(locks!=1)++errors;--locks;}
void svcSignalProcessWideKey(u32 *key,s32 n){(void)key;(void)n;}
void svcSleepThread(s64 n){(void)n;++sleeps;}
Result threadCreate(Thread *t,ThreadFunc f,void *a,void *s,size_t n,int p,int c){(void)t;(void)f;(void)a;(void)s;(void)n;(void)p;(void)c;++creates;return 0;}
Result threadStart(Thread *t){(void)t;return 0;}
Result threadClose(Thread *t){(void)t;return 0;}
Result threadWaitForExit(Thread *t){(void)t;++joins;return 0;}
Result condvarWaitTimeout(CondVar *c,Mutex *m,u64 timeout){
    (void)c;(void)m;(void)timeout;
    if(locks!=1)++errors;
    locks=0;bool was_running=running;running=false;cache_worker(NULL);running=was_running;locks=1;return 0;
}
int mkdir(const char *p,mode_t m){(void)p;(void)m;if(locks)++errors;++io;return 0;}
FILE *fopen(const char *p,const char *m){(void)m;if(locks)++errors;++io;snprintf(opened,sizeof(opened),"%s",p);return (FILE*)1;}
int fclose(FILE *f){(void)f;if(locks)++errors;++io;return 0;}
int fflush(FILE *f){(void)f;if(locks)++errors;++io;if(flush_failures){--flush_failures;return -1;}return 0;}
int remove(const char *p){(void)p;if(locks)++errors;++io;return 0;}
int rename(const char *a,const char *b){(void)a;(void)b;if(locks)++errors;++io;return 0;}
size_t fwrite(const void *p,size_t size,size_t n,FILE *f){
    (void)f;if(locks)++errors;++io;++writes;
    const unsigned char *bytes=p;
    if(!strcmp(opened,"ra/user/unlocks.bin.tmp")){
        ++critical_writes;
        if(critical_writes<=2){
            unsigned value=critical_writes==1 ? 2:3;
            for(unsigned i=0;i<size*n;++i)if(bytes[i]!=value)++errors;
        }
        if(inject){
            inject=false;
            if(!CacheOunce_Queue("ra/user/unlocks.bin",NULL,0,newest,sizeof(newest),true))++errors;
            for(unsigned i=0;i<size*n;++i)if(bytes[i]!=2)++errors;
        }
    }else if(writes==1)++errors; /* Unlock journals must be selected first. */
    return n;
}

int overlay_test(void){
    unsigned char prefix[16],data[32];for(unsigned i=0;i<16;++i)prefix[i]=1;for(unsigned i=0;i<32;++i){data[i]=1;newest[i]=3;}
    if(CacheOunce_Queue("ra/user/unlocks.bin",prefix,16,data,32,true))return 1;
    if(!CacheOunce_Init() || !CacheOunce_Init() || creates!=1)return 2;
    for(unsigned i=0;i<JOB_COUNT;++i){char name[40]="ra/badges/0.rgba";name[10]='0'+i;if(!CacheOunce_Queue(name,NULL,0,data,32,false))return 3;}
    if(!CacheOunce_Queue("ra/user/unlocks.bin",prefix,16,data,32,true) || count!=JOB_COUNT)return 4;
    for(unsigned i=0;i<16;++i)prefix[i]=2;
    for(unsigned i=0;i<32;++i)data[i]=2;
    if(!CacheOunce_Queue("ra/user/unlocks.bin",prefix,16,data,32,true) || count!=JOB_COUNT)return 5;
    /* The queued snapshot owns its bytes and authenticated prefix. */
    for(unsigned i=0;i<16;++i)prefix[i]=9;
    for(unsigned i=0;i<32;++i)data[i]=9;
    if(io || writes)return 6;
    inject=true;CacheOunce_Flush();
    if(count || writing || locks || errors || critical_writes!=2 || writes!=JOB_COUNT+1)return 7;
    if(CacheOunce_RecordRetryNeeded())return 8;
    /* Critical failure is surfaced for a later retry, never a frame-thread
     * synchronous write. A badge failure does not dirty the unlock journal. */
    if(!CacheOunce_Queue("ra/user/unlocks.bin",NULL,0,data,32,true))return 9;
    flush_failures=3;CacheOunce_Flush();
    if(!CacheOunce_RecordRetryNeeded() || CacheOunce_RecordRetryNeeded() || sleeps!=2)return 10;
    CacheOunce_Close();
    if(started || joins!=1 || errors)return 11;
    return 0;
}
