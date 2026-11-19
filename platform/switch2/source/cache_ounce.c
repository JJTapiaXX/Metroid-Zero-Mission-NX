/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "cache_ounce.h"
#include "diagnostics.h"

enum { JOB_COUNT=8, JOB_BYTES=8192, PATH_BYTES=256 };
typedef struct {
    char path[PATH_BYTES];
    unsigned size;
    bool backup;
    unsigned char data[JOB_BYTES];
} CacheJob;
static CacheJob jobs[JOB_COUNT];
static unsigned count;
static bool started,running,writing,record_failed;
static Thread worker;
static Mutex lock;
static CondVar wake;

static bool write_job(const CacheJob *job) {
    char temporary[PATH_BYTES+8],backup[PATH_BYTES+8];
    snprintf(temporary,sizeof(temporary),"%s.tmp",job->path);
    snprintf(backup,sizeof(backup),"%s.bak",job->path);
    /* Badge directories may not exist yet. Scope directories for signed
     * unlock records are created during account initialization. */
    if (!job->backup) { mkdir("ra",0777);mkdir("ra/badges",0777); }
    FILE *f=fopen(temporary,"wb");
    if (!f) return false;
    bool okay=fwrite(job->data,1,job->size,f)==job->size;
    if (fflush(f)) okay=false;
    if (fclose(f)) okay=false;
    if (!okay) { remove(temporary);return false; }
    if (job->backup) {
        FILE *current=fopen(job->path,"rb");
        if (current) {
            fclose(current);remove(backup);
            if (rename(job->path,backup)) {remove(temporary);return false;}
        }
        /* If a previous rename was interrupted and current is absent,
         * preserve the complete backup while retrying the temp install. */
    }
    else remove(job->path);
    return rename(temporary,job->path)==0;
}

static void cache_worker(void *unused) {
    (void)unused;
    CacheJob job;
    for (;;) {
        mutexLock(&lock);
        while (!count && running) condvarWait(&wake,&lock);
        if (!count && !running) { mutexUnlock(&lock);break; }
        /* Unlock journals take precedence over optional badge images. The
         * writer owns its copy, so a producer can replace a newer queued
         * snapshot without ever changing bytes being written to disk. */
        unsigned selected=0;
        for (unsigned i=0;i<count;++i) if (jobs[i].backup) { selected=i;break; }
        job=jobs[selected];
        memmove(jobs+selected,jobs+selected+1,(count-selected-1)*sizeof(*jobs));
        --count;writing=true;mutexUnlock(&lock);
        bool okay=false;
        for (unsigned attempt=0;attempt<3 && !okay;++attempt) {
            okay=write_job(&job);
            if (!okay && attempt<2) svcSleepThread(100000000);
        }
        if (!okay) Diagnostics_Event(job.backup ? "achievement cache write failed":"badge cache write failed");
        mutexLock(&lock);
        if (!okay && job.backup) record_failed=true;
        writing=false;condvarWakeAll(&wake);mutexUnlock(&lock);
    }
}

bool CacheOunce_Init(void) {
    if (started) return true;
    running=true;
    Result rc=threadCreate(&worker,cache_worker,NULL,NULL,0x10000,0x36,-2);
    if (R_SUCCEEDED(rc)) {
        rc=threadStart(&worker);
        if (R_FAILED(rc)) threadClose(&worker);
    }
    started=R_SUCCEEDED(rc);
    if (!started) { running=false;Diagnostics_Event("achievement cache worker unavailable"); }
    return started;
}

bool CacheOunce_Queue(const char *path,const void *prefix,size_t prefix_size,
                      const void *data,size_t size,bool keep_backup) {
    if (!started || !path || !*path || strlen(path)>=PATH_BYTES ||
        prefix_size>JOB_BYTES || size>JOB_BYTES-prefix_size ||
        (prefix_size && !prefix) || (size && !data)) return false;
    mutexLock(&lock);
    unsigned slot=count;
    for (unsigned i=0;i<count;++i) if (!strcmp(jobs[i].path,path)) { slot=i;break; }
    if (slot==count && count==JOB_COUNT) {
        /* Drop optional cache work rather than making a journal wait for SD
         * I/O. A failed critical enqueue is reported to the caller for retry. */
        if (keep_backup) for (unsigned i=0;i<count;++i) if (!jobs[i].backup) { slot=i;break; }
        if (slot==count) { mutexUnlock(&lock);return false; }
    }
    CacheJob *job=&jobs[slot];
    snprintf(job->path,sizeof(job->path),"%s",path);
    job->size=(unsigned)(prefix_size+size);job->backup=keep_backup;
    if (prefix_size) memcpy(job->data,prefix,prefix_size);
    if (size) memcpy(job->data+prefix_size,data,size);
    if (slot==count) ++count;
    condvarWakeOne(&wake);mutexUnlock(&lock);return true;
}

void CacheOunce_Flush(void) {
    if (!started) return;
    mutexLock(&lock);
    while (count || writing) condvarWait(&wake,&lock);
    mutexUnlock(&lock);
}

bool CacheOunce_RecordRetryNeeded(void) {
    mutexLock(&lock);bool failed=record_failed;record_failed=false;mutexUnlock(&lock);return failed;
}

void CacheOunce_Close(void) {
    if (!started) return;
    CacheOunce_Flush();
    mutexLock(&lock);running=false;condvarWakeAll(&wake);mutexUnlock(&lock);
    threadWaitForExit(&worker);threadClose(&worker);started=false;
}
