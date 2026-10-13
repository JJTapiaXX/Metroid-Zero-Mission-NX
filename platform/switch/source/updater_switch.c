/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include "updater_switch.h"
#include "network_switch.h"
#include "rc_api_common.h"
extern int NetworkSwitch_PublicGet(const char*,char**,size_t*,size_t);
static Thread worker_thread;
static Mutex lock;
static CondVar wake;
static bool started,running;
static unsigned job;
static atomic_uint state;
static char target[512]="sdmc:/switch/mzm/mzmnx.nro",asset_url[2048],expected_hash[65],release_name[64];
static uint32_t expected_size;
static char asset_time[21];
#ifndef NX_UPDATER_BUILD_TIME
#define NX_UPDATER_BUILD_TIME "2000-01-01T00:00:00Z"
#endif
static char status_text[96]="CHECK FOR A GITHUB UPDATE",display_text[96];
static void status(unsigned value,const char *text) {
    mutexLock(&lock);snprintf(status_text,sizeof(status_text),"%s",text);atomic_store(&state,value);mutexUnlock(&lock);
}
const char *Updater_Status(void) {
    mutexLock(&lock);memcpy(display_text,status_text,sizeof(display_text));mutexUnlock(&lock);return display_text;
}
const char *Updater_Button(void) {
    unsigned s=atomic_load(&state);
    return s==1 ? "CHECKING..." : s==3 ? "DOWNLOADING..." : s==2 ? "DOWNLOAD AND INSTALL" : s==5 ? "UPDATE INSTALLED" : "CHECK FOR UPDATES";
}
static void hex_hash(const uint8_t hash[32],char text[65]) {
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<32;++i){text[i*2]=digits[hash[i]>>4];text[i*2+1]=digits[hash[i]&15];}text[64]=0;
}
static bool file_hash(const char *path,char out[65]) {
    FILE *f=fopen(path,"rb");if(!f)return false;
    Sha256Context sha;sha256ContextCreate(&sha);uint8_t buffer[8192];size_t n;
    while((n=fread(buffer,1,sizeof(buffer),f)))sha256ContextUpdate(&sha,buffer,n);
    bool okay=!ferror(f);if(fclose(f))okay=false;
    if(okay){uint8_t hash[32];sha256ContextGetHash(&sha,hash);hex_hash(hash,out);}return okay;
}
static bool parse_release(const char *body,size_t length) {
    rc_json_field_t root[]={RC_JSON_NEW_FIELD("tag_name"),RC_JSON_NEW_FIELD("assets")};
    rc_json_iterator_t iterator={body,body+length};rc_buffer_t buffer;rc_buffer_init(&buffer);bool found=false;
    if(!rc_json_get_array_entry_object(root,2,&iterator) || !root[1].value_start || *root[1].value_start!='[')goto done;
    const char *tag=NULL;if(!rc_json_get_string(&tag,&buffer,&root[0],"tag_name") || !tag)goto done;
    snprintf(release_name,sizeof(release_name),"%.63s",tag);
    iterator=(rc_json_iterator_t){root[1].value_start+1,root[1].value_end-1};
    while(iterator.json<iterator.end) {
        rc_json_field_t asset[]={RC_JSON_NEW_FIELD("name"),RC_JSON_NEW_FIELD("browser_download_url"),RC_JSON_NEW_FIELD("digest"),RC_JSON_NEW_FIELD("size"),RC_JSON_NEW_FIELD("updated_at")};
        if(!rc_json_get_array_entry_object(asset,5,&iterator))break;
        if(!rc_json_field_string_matches(&asset[0],"mzmnx.nro"))continue;
        const char *url=NULL,*digest=NULL;uint32_t size=0;
        if(!rc_json_get_string(&url,&buffer,&asset[1],"browser_download_url") || !rc_json_get_string(&digest,&buffer,&asset[2],"digest") || !rc_json_get_unum(&size,&asset[3],"size"))break;
        const char *prefix="https://github.com/JJTapiaXX/Metroid-Zero-Mission-NX/releases/download/";
        if(!url || strncmp(url,prefix,strlen(prefix)) || strlen(url)>=sizeof(asset_url) || !digest || strncmp(digest,"sha256:",7) || strlen(digest)!=71 || size<0x80 || size>16700000)break;
        for(unsigned i=7;i<71;++i)if(!((digest[i]>='0'&&digest[i]<='9')||(digest[i]>='a'&&digest[i]<='f')))goto done;
        const char *updated=NULL;
        if(!rc_json_get_string(&updated,&buffer,&asset[4],"updated_at") || !updated || strlen(updated)!=20 ||
            updated[4]!='-' || updated[7]!='-' || updated[10]!='T' || updated[13]!=':' || updated[16]!=':' || updated[19]!='Z')break;
        strcpy(asset_time,updated);
        strcpy(asset_url,url);strcpy(expected_hash,digest+7);expected_size=size;found=true;break;
    }
done:rc_buffer_destroy(&buffer);return found;
}
static uint32_t word(const uint8_t *p){uint32_t value;memcpy(&value,p,4);return value;}
static uint64_t qword(const uint8_t *p){uint64_t value;memcpy(&value,p,8);return value;}
static bool valid_nro(const uint8_t *data,size_t size) {
    if(size<0x80 || memcmp(data+0x10,"NRO0",4))return false;
    size_t end=word(data+0x18);if(end>size || size-end<56 || memcmp(data+end,"ASET",4))return false;
    uint64_t offset=qword(data+end+24),length=qword(data+end+32);
    if(offset>size-end || length>size-end-offset || length<0x4000)return false;
    const uint8_t *nacp=data+end+offset;
    return !memcmp(nacp,"Metroid: Zero Mission NX\0",24) && !memcmp(nacp+0x200,"JJTapia_XX\0",11);
}
/* Keep the worker's transfer/installation stack separate from its wait loop. */
__attribute__((noinline)) void Updater_Process(unsigned operation) {
    char *body=NULL;size_t length=0;
    if(operation==1) {
        status(1,"CHECKING GITHUB...");
        int http=NetworkSwitch_PublicGet("https://api.github.com/repos/JJTapiaXX/Metroid-Zero-Mission-NX/releases/latest",&body,&length,1048576);
        if(http!=200 || !body){free(body);status(6,http==403 || http==429 ? "GITHUB RATE LIMIT - TRY LATER":"CHECK FAILED - TRY AGAIN");return;}
        bool okay=parse_release(body,length);free(body);
        if(!okay){status(6,"NO VALID NRO IN LATEST RELEASE");return;}
        char current[65];
        if(file_hash(target,current) && !strcmp(current,expected_hash)){status(4,"YOU HAVE THE LATEST FILE");return;}
        if(strcmp(asset_time,NX_UPDATER_BUILD_TIME)<0){status(4,"NO NEWER RELEASE AVAILABLE");return;}
        char label[96];snprintf(label,sizeof(label),"LATEST: %.63s",release_name);status(2,label);return;
    }
    status(3,"DOWNLOADING LATEST NRO...");
    int http=NetworkSwitch_PublicGet(asset_url,&body,&length,16777216);
    if(http!=200 || !body || length!=expected_size){free(body);status(6,"DOWNLOAD FAILED - TRY AGAIN");return;}
    uint8_t hash[32];char digest[65];sha256CalculateHash(hash,body,length);hex_hash(hash,digest);
    if(strcmp(digest,expected_hash) || !valid_nro((uint8_t*)body,length)){free(body);status(6,"DOWNLOAD VERIFICATION FAILED");return;}
    char temporary[544],backup[544];snprintf(temporary,sizeof(temporary),"%s.update",target);snprintf(backup,sizeof(backup),"%s.bak",target);
    FILE *f=fopen(temporary,"wb");bool okay=f!=NULL;
    if(f){okay=fwrite(body,1,length,f)==length;if(fflush(f))okay=false;if(fclose(f))okay=false;}free(body);
    if(!okay){remove(temporary);status(6,"CANNOT WRITE UPDATE TO SD");return;}
    char written[65];if(!file_hash(temporary,written) || strcmp(written,expected_hash)){remove(temporary);status(6,"SD VERIFICATION FAILED");return;}
    FILE *installed=fopen(target,"rb");bool had_original=installed!=NULL;if(installed)fclose(installed);
    if(had_original){remove(backup);if(rename(target,backup)){remove(temporary);status(6,"CANNOT BACK UP INSTALLED NRO");return;}}
    if(rename(temporary,target)){
        if(had_original && rename(backup,target)){status(6,"RESTORE .BAK NRO ON YOUR SD");return;}
        remove(temporary);status(6,"INSTALL FAILED - ORIGINAL RESTORED");return;
    }
    status(5,"INSTALLED - CLOSE AND RELAUNCH APP");
}
static void worker(void *unused) {
    (void)unused;mutexLock(&lock);
    while(running){while(running && !job)condvarWait(&wake,&lock);if(!running)break;unsigned next=job;job=0;mutexUnlock(&lock);Updater_Process(next);mutexLock(&lock);}
    mutexUnlock(&lock);
}
void Updater_Init(const char *path) {
    if(path && !strncmp(path,"sdmc:/",6) && strlen(path)<sizeof(target) && strlen(path)>4 && !strcmp(path+strlen(path)-4,".nro"))snprintf(target,sizeof(target),"%s",path);
}
void Updater_Action(void) {
    unsigned s=atomic_load(&state);if(s==1 || s==3 || s==5)return;
    if(!NetworkSwitch_Init()){status(6,"NETWORK INITIALIZATION FAILED");return;}
    if(!started){
        running=true;Result rc=threadCreate(&worker_thread,worker,NULL,NULL,0x18000,0x31,-2);
        if(R_SUCCEEDED(rc)){rc=threadStart(&worker_thread);if(R_FAILED(rc))threadClose(&worker_thread);}
        if(R_FAILED(rc)){running=false;status(6,"UPDATER COULD NOT START");return;}
        started=true;
    }
    mutexLock(&lock);job=s==2 ? 2:1;atomic_store(&state,job==1 ? 1:3);condvarWakeOne(&wake);mutexUnlock(&lock);
}
void Updater_Close(void) {
    if(!started)return;
    mutexLock(&lock);running=false;condvarWakeOne(&wake);mutexUnlock(&lock);threadWaitForExit(&worker_thread);threadClose(&worker_thread);started=false;
}
