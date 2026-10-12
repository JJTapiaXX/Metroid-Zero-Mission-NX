/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "achievements_switch.h"
#include "achievement_badges.h"
#include "network_switch.h"
#include "native_memory.h"
#include "rom_hash.h"
#include "screen_overlay.h"
#include "audio_switch.h"
#include "rc_runtime.h"
#include "rc_runtime_types.h"
#include "rc_api_user.h"
#include "rc_api_runtime.h"
#include "rc_error.h"
#include "md5.h"
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#define MAX_ACHIEVEMENTS 256
typedef struct { uint32_t id,flags; int64_t when; } Record;
static char username[64],token[64],password[128],status_message[64]="DISABLED";
static char scope[160];
static bool enabled,loaded,session_ready;
static unsigned badge_index,toast_index=UINT32_MAX;
static unsigned stage,award_index,item_count;
static uint64_t retry_after;
static rc_runtime_t runtime;
static rc_api_fetch_game_data_response_t *game;
static Record records[MAX_ACHIEVEMENTS];
static unsigned record_count;
static unsigned indices[MAX_ACHIEVEMENTS];
static unsigned triggered[MAX_ACHIEVEMENTS],triggered_count,invalid_addresses[MAX_ACHIEVEMENTS],invalid_count;
static bool evaluating;
static void status(const char *message) { snprintf(status_message,sizeof(status_message),"%s",message); }
static void hmac(const char *name,const uint8_t *data,size_t size,uint8_t digest[16]) {
    uint8_t key[64]={0},inner[16],pad[64]; size_t length=strlen(token);
    if (length>64) length=64;
    memcpy(key,token,length);
    for (unsigned i=0;i<64;++i) pad[i]=key[i]^0x36;
    md5_state_t m; md5_init(&m); md5_append(&m,pad,64); md5_append(&m,(const uint8_t*)scope,strlen(scope)); md5_append(&m,(const uint8_t*)name,strlen(name)); md5_append(&m,data,size); md5_finish(&m,inner);
    for (unsigned i=0;i<64;++i) pad[i]=key[i]^0x5c;
    md5_init(&m); md5_append(&m,pad,64); md5_append(&m,inner,16); md5_finish(&m,digest);
}
static void make_scope(void) {
    char lower[64]; for (unsigned i=0;i<sizeof(lower);++i) { lower[i]=tolower((unsigned char)username[i]); if (!lower[i]) break; }
    md5_state_t m; uint8_t hash[16]; char user_hash[33];
    md5_init(&m); md5_append(&m,(const uint8_t*)lower,strlen(lower)); md5_finish(&m,hash);
    for (unsigned i=0;i<16;++i) { user_hash[i*2]="0123456789abcdef"[hash[i]>>4]; user_hash[i*2+1]="0123456789abcdef"[hash[i]&15]; }
    user_hash[32]=0; snprintf(scope,sizeof(scope),"ra/%s/%s",RomHash_String(),user_hash);
    mkdir("ra",0777); char dir[64]; snprintf(dir,sizeof(dir),"ra/%s",RomHash_String()); mkdir(dir,0777); mkdir(scope,0777);
}
static bool write_cache(const char *name,const void *data,size_t size) {
    char path[224],temp[224],backup[224]; snprintf(path,sizeof(path),"%s/%s",scope,name);
    snprintf(temp,sizeof(temp),"%s/%s.tmp",scope,name); snprintf(backup,sizeof(backup),"%s/%s.bak",scope,name);
    uint8_t digest[16]; hmac(name,data,size,digest);
    FILE *f=fopen(temp,"wb"); if (!f) return false;
    bool okay=fwrite(digest,1,16,f)==16 && fwrite(data,1,size,f)==size;
    int flush=fflush(f); if (fclose(f) || flush) okay=false;
    if (okay) { remove(backup); rename(path,backup); okay=rename(temp,path)==0; }
    return okay;
}
static uint8_t *read_cache(const char *name,size_t *size) {
    for (unsigned backup=0;backup<2;++backup) {
        char path[224]; snprintf(path,sizeof(path),"%s/%s%s",scope,name,backup ? ".bak" : "");
        FILE *f=fopen(path,"rb"); if (!f) continue;
        if (fseek(f,0,SEEK_END)) { fclose(f); continue; }
        long length=ftell(f); rewind(f);
        if (length<16 || length>1048592) { fclose(f); continue; }
        uint8_t *data=malloc(length-16+1),digest[16],actual[16];
        bool okay=data && fread(digest,1,16,f)==16 && fread(data,1,length-16,f)==(size_t)length-16;
        fclose(f); if (!okay) { free(data); continue; }
        hmac(name,data,length-16,actual);
        if (memcmp(actual,digest,16)) { free(data); continue; }
        data[length-16]=0; *size=length-16; return data;
    }
    return NULL;
}
static void save_records(void) { write_cache("unlocks.bin",records,record_count*sizeof(Record)); }
static void load_records(void) {
    size_t size=0; uint8_t *data=read_cache("unlocks.bin",&size); record_count=0;
    if (data && size%sizeof(Record)==0 && size/sizeof(Record)<=MAX_ACHIEVEMENTS) {
        record_count=size/sizeof(Record); memcpy(records,data,size);
        for (unsigned i=0;i<record_count;++i) if (records[i].flags>2 || !records[i].id) { record_count=0; break; }
    }
    free(data);
}
static Record *record(unsigned id,bool create) {
    for (unsigned i=0;i<record_count;++i) if (records[i].id==id) return &records[i];
    if (!create || record_count==MAX_ACHIEVEMENTS) return NULL;
    Record *r=&records[record_count++]; *r=(Record){id,0,0}; return r;
}
static uint32_t peek(uint32_t address,uint32_t bytes,void *unused) {
    (void)unused; uint32_t value=0;
    if (bytes>4) return 0;
    for (unsigned i=0;i<bytes;++i) { uint8_t byte; if (!NativeMemory_ReadByte(address+i,&byte)) {
            if (evaluating && invalid_count<MAX_ACHIEVEMENTS) invalid_addresses[invalid_count++]=address;
            return 0;
        } value|=(uint32_t)byte<<(i*8); }
    return value;
}
static int valid(uint32_t address) { uint8_t value; return NativeMemory_ReadByte(address,&value); }
static void event(const rc_runtime_event_t *e) {
    if (e->type!=RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED) return;
    if (evaluating) { if (triggered_count<MAX_ACHIEVEMENTS) triggered[triggered_count++]=e->id; return; }
    Record *r=record(e->id,true); if (!r || r->flags) return;
    r->flags=2; r->when=time(NULL); save_records();
    for (unsigned i=0;i<item_count;++i) if (game->achievements[indices[i]].id==e->id) {
        char detail[64]; snprintf(detail,sizeof(detail),"%u POINTS EARNED",game->achievements[indices[i]].points);
        SwitchOverlay_Toast(game->achievements[indices[i]].title,detail);
        toast_index=i; SwitchOverlay_ToastBadge(Achievements_Badge(i)); break;
    }
}
static bool set_game(const char *json,bool cached) {
    rc_api_fetch_game_data_response_t *parsed=calloc(1,sizeof(*parsed));
    if (!parsed) return false;
    int result=rc_api_process_fetch_game_data_response(parsed,json);
    if (result!=RC_OK || !parsed->response.succeeded || parsed->console_id!=5 || !parsed->num_achievements) {
        rc_api_destroy_fetch_game_data_response(parsed); free(parsed); status("SET NOT AVAILABLE"); return false;
    }
    bool same=loaded && parsed->id==game->id && parsed->num_achievements==game->num_achievements;
    if (same) for (unsigned i=0;i<game->num_achievements;++i)
        if (parsed->achievements[i].id!=game->achievements[i].id || strcmp(parsed->achievements[i].definition,game->achievements[i].definition)) { same=false; break; }
    if (same) {
        rc_api_destroy_fetch_game_data_response(game); free(game); game=parsed;
        for (unsigned i=0;i<item_count;++i) Badges_Register(i,game->achievements[indices[i]].badge_name);
        status(cached ? "OFFLINE - CACHED SOFTCORE" : "CONNECTED - SOFTCORE"); return true;
    }
    if (loaded) { rc_runtime_destroy(&runtime); rc_api_destroy_fetch_game_data_response(game); free(game); }
    game=parsed; rc_runtime_init(&runtime); loaded=true; item_count=0; Badges_Reset();
    for (unsigned i=0;i<game->num_achievements && item_count<MAX_ACHIEVEMENTS;++i) {
        rc_api_achievement_definition_t *a=&game->achievements[i];
        if (a->category!=3 || !a->definition || strlen(a->definition)>16384) continue;
        if (rc_runtime_activate_achievement(&runtime,a->id,a->definition,NULL,0)!=RC_OK) continue;
        Badges_Register(item_count,a->badge_name); indices[item_count++]=i;
        Record *r=record(a->id,false); if (r && r->flags) rc_runtime_deactivate_achievement(&runtime,a->id);
    }
    rc_runtime_validate_addresses(&runtime,event,valid);
    status(cached ? "OFFLINE - CACHED SOFTCORE" : "CONNECTED - SOFTCORE"); return true;
}
static void profile_save(void) {
    FILE *f=fopen("ra-account.cfg.tmp","wb"); if (!f) return;
    fprintf(f,"%s\n%s\n",username,token); int flush=fflush(f); if (fclose(f) || flush) return;
    remove("ra-account.cfg.bak"); rename("ra-account.cfg","ra-account.cfg.bak"); rename("ra-account.cfg.tmp","ra-account.cfg");
}
static bool send(unsigned next,rc_api_request_t *r) {
    bool okay=NetworkSwitch_Request(r->url,r->post_data); rc_api_destroy_request(r);
    if (okay) stage=next;
    else { stage=0; session_ready=false; retry_after=AudioSwitch_NowNs()+30000000000ULL; status(loaded ? "OFFLINE - CACHED SOFTCORE" : "NETWORK UNAVAILABLE"); }
    return okay;
}
void Achievements_Reconnect(void) {
    enabled=true;
    FILE *preference=fopen("ra-enabled.cfg","wb"); if (preference) { fputc(1,preference); fclose(preference); }
    if (stage || !username[0] || (!token[0] && !password[0])) return;
    enabled=true; rc_api_request_t request={0};
    rc_api_login_request_t params={username,token[0] ? token : NULL,password[0] ? password : NULL};
    if (rc_api_init_login_request(&request,&params)==RC_OK) send(1,&request);
    memset(password,0,sizeof(password));
}
void Achievements_Init(void) {
    FILE *preference=fopen("ra-enabled.cfg","rb");
    int flag=-1; if (preference) { flag=fgetc(preference); fclose(preference); }
    enabled=flag==1;
    FILE *f=fopen("ra-account.cfg","rb");
    if (!f) f=fopen("ra-account.cfg.bak","rb");
    if (!f) { if (enabled) status("SIGN IN REQUIRED"); return; }
    if (!fgets(username,sizeof(username),f) || !fgets(token,sizeof(token),f)) username[0]=token[0]=0;
    fclose(f); username[strcspn(username,"\r\n")]=0; token[strcspn(token,"\r\n")]=0;
    if (!username[0] || !token[0]) { if (enabled) status("SIGN IN REQUIRED"); return; }
    if (flag==0) return;
    enabled=true; make_scope(); load_records(); size_t size=0; uint8_t *json=read_cache("set.json",&size);
    if (json) { set_game((char*)json,true); free(json); }
    Achievements_Reconnect();
}
static bool keyboard(char *buffer,size_t size,const char *title,bool secret) {
    SwkbdConfig config; if (R_FAILED(swkbdCreate(&config,0))) return false;
    if (secret) swkbdConfigMakePresetPassword(&config); else swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetHeaderText(&config,title); swkbdConfigSetStringLenMax(&config,size-1);
    Result rc=swkbdShow(&config,buffer,size); swkbdClose(&config); return R_SUCCEEDED(rc) && buffer[0];
}
void Achievements_Login(void) {
    if (stage) { status("REQUEST IN PROGRESS"); return; }
    char user[64]={0},pass[128]={0};
    if (!keyboard(user,sizeof(user),"RetroAchievements username",false) || !keyboard(pass,sizeof(pass),"RetroAchievements password",true)) { memset(pass,0,sizeof(pass)); return; }
    if (strcasecmp(user,username)) { token[0]=0; record_count=0; if (loaded) { rc_runtime_destroy(&runtime); rc_api_destroy_fetch_game_data_response(game); free(game); loaded=false; item_count=0; } }
    snprintf(username,sizeof(username),"%s",user); snprintf(password,sizeof(password),"%s",pass); memset(pass,0,sizeof(pass));
    Achievements_Reconnect(); status("SIGNING IN");
}
void Achievements_Disable(void) {
    enabled=false; status("DISABLED");
    FILE *f=fopen("ra-enabled.cfg","wb"); if (f) { fputc(0,f); fclose(f); }
}
void Achievements_Update(void) {
    if (!enabled) return;
    char *body=NULL; int http=0;
    if (stage && NetworkSwitch_Poll(&body,&http)) {
        unsigned current=stage; stage=0;
        if (current==5) {
            bool okay=http==200 && body && Badges_Decode(badge_index,body,NetworkSwitch_ResponseSize());
            if (!okay) Badges_Failed(badge_index);
            else if (toast_index==badge_index) SwitchOverlay_ToastBadge(Badges_Get(badge_index));
            free(body); return;
        }
        if (http!=200 || !body) {
            free(body); session_ready=false; retry_after=AudioSwitch_NowNs()+30000000000ULL;
            status(loaded ? "OFFLINE - CACHED SOFTCORE" : NetworkSwitch_Error()); return;
        }
        rc_api_request_t request={0};
        if (current==1) {
            rc_api_login_response_t response={0}; int result=rc_api_process_login_response(&response,body);
            if (result==RC_OK && response.response.succeeded && response.api_token && response.username) {
                Record previous[MAX_ACHIEVEMENTS]; unsigned previous_count=record_count;
                memcpy(previous,records,previous_count*sizeof(Record));
                size_t cached_size=0; uint8_t *cached=scope[0] && token[0] ? read_cache("set.json",&cached_size) : NULL;
                snprintf(username,sizeof(username),"%s",response.username); snprintf(token,sizeof(token),"%s",response.api_token);
                make_scope(); load_records();
                for (unsigned i=0;i<previous_count;++i) {
                    Record *r=record(previous[i].id,true); if (r && previous[i].flags>r->flags) *r=previous[i];
                }
                if (cached) { write_cache("set.json",cached,cached_size); free(cached); }
                save_records(); profile_save();
                rc_api_fetch_game_data_request_t params={username,token,0,RomHash_String()};
                if (rc_api_init_fetch_game_data_request(&request,&params)==RC_OK) send(2,&request);
            } else { status("SIGN IN FAILED - RETRY LOGIN"); retry_after=UINT64_MAX; }
            rc_api_destroy_login_response(&response);
        } else if (current==2) {
            if (set_game(body,false)) {
                write_cache("set.json",body,strlen(body));
                rc_api_start_session_request_t params={username,token,game->id,RomHash_String(),0};
                if (rc_api_init_start_session_request(&request,&params)==RC_OK) send(3,&request);
            }
        } else if (current==3) {
            rc_api_start_session_response_t response={0}; int result=rc_api_process_start_session_response(&response,body);
            if (result==RC_OK && response.response.succeeded) {
                for (unsigned i=0;i<response.num_unlocks;++i) {
                    Record *r=record(response.unlocks[i].achievement_id,true); if (r) { r->flags=1; r->when=response.unlocks[i].when; }
                    rc_runtime_deactivate_achievement(&runtime,response.unlocks[i].achievement_id);
                }
                for (unsigned i=0;i<response.num_hardcore_unlocks;++i) {
                    Record *r=record(response.hardcore_unlocks[i].achievement_id,true); if (r) r->flags=1;
                    rc_runtime_deactivate_achievement(&runtime,response.hardcore_unlocks[i].achievement_id);
                }
                save_records(); session_ready=true; status("CONNECTED - SOFTCORE");
            } else { session_ready=false; status("SESSION ERROR - RETRY"); retry_after=AudioSwitch_NowNs()+30000000000ULL; }
            rc_api_destroy_start_session_response(&response);
        } else if (current==4) {
            rc_api_award_achievement_response_t response={0}; int result=rc_api_process_award_achievement_response(&response,body);
            if (result==RC_OK && response.response.succeeded && award_index<record_count &&
                (response.awarded_achievement_id==0 || response.awarded_achievement_id==records[award_index].id)) {
                records[award_index].flags=1; save_records(); status("CONNECTED - UNLOCK SYNCED");
            } else { session_ready=false; retry_after=AudioSwitch_NowNs()+30000000000ULL; status("UNLOCK PENDING - RETRY"); }
            rc_api_destroy_award_achievement_response(&response);
        }
        free(body);
    }
    if (!stage && session_ready && loaded) {
        for (unsigned i=0;i<record_count;++i) if (records[i].flags==2) {
            rc_api_request_t request={0}; time_t now=time(NULL); int64_t age=now-records[i].when;
            rc_api_award_achievement_request_t params={username,token,records[i].id,0,RomHash_String(),age>0 ? (age>UINT32_MAX ? UINT32_MAX : age) : 0};
            award_index=i; if (rc_api_init_award_achievement_request(&request,&params)==RC_OK) send(4,&request); break;
        }
        if (!stage) {
            int next=toast_index<item_count && Badges_Needs(toast_index) ? (int)toast_index : Badges_Next();
            if (next>=0) {
                badge_index=(unsigned)next;
                rc_api_request_t image={0};
                rc_api_fetch_image_request_t params={game->achievements[indices[badge_index]].badge_name,RC_IMAGE_TYPE_ACHIEVEMENT};
                if (rc_api_init_fetch_image_request(&image,&params)==RC_OK) {
                    if (NetworkSwitch_Request(image.url,NULL)) stage=5;
                    else Badges_Failed(badge_index);
                    rc_api_destroy_request(&image);
                } else Badges_Failed(badge_index);
            }
        }
    } else if (!stage && !session_ready && token[0] && AudioSwitch_NowNs()>=retry_after) Achievements_Reconnect();
}
void Achievements_Evaluate(void) {
    if (!enabled || !loaded) return;
    triggered_count=invalid_count=0; evaluating=true;
    rc_runtime_do_frame(&runtime,event,peek,NULL,NULL); evaluating=false;
    for (unsigned i=0;i<invalid_count;++i) rc_runtime_invalidate_address(&runtime,invalid_addresses[i]);
    if (invalid_count) rc_runtime_validate_addresses(&runtime,event,valid);
    for (unsigned i=0;i<triggered_count;++i) {
        rc_trigger_t *trigger=rc_runtime_get_achievement(&runtime,triggered[i]);
        if (trigger && trigger->state!=RC_TRIGGER_STATE_DISABLED) {
            rc_runtime_event_t unlocked={triggered[i],0,RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED}; event(&unlocked);
        }
    }
}
const uint32_t *Achievements_Badge(unsigned index) {
    return loaded && index<item_count ? Badges_Get(index) : NULL;
}
unsigned Achievements_Earned(void) {
    unsigned count=0; for (unsigned i=0;i<Achievements_Count();++i) if (Achievements_Unlocked(i)) ++count; return count;
}
const char *Achievements_ConnectionLabel(void) {
    if (!enabled) return "ACHIEVEMENTS OFF";
    if (session_ready) return Achievements_Pending() ? "SYNCING UNLOCKS" : "CONNECTED";
    if (stage) return "CONNECTING";
    if (loaded) return "OFFLINE - UNLOCKS SAVED";
    return token[0] ? "CONNECTION FAILED - RETRY" : "SIGN IN TO START";
}
const char *Achievements_Status(void) { return status_message; }
unsigned Achievements_Count(void) { return loaded ? item_count : 0; }
__attribute__((noinline)) unsigned Achievements_Pending(void) { unsigned count=0; for (unsigned i=0;i<record_count;++i) if (records[i].flags==2) ++count; return count; }
const char *Achievements_Title(unsigned index) { return loaded && index<item_count ? game->achievements[indices[index]].title : ""; }
const char *Achievements_Description(unsigned index) { return loaded && index<item_count ? game->achievements[indices[index]].description : ""; }
unsigned Achievements_Points(unsigned index) { return loaded && index<item_count ? game->achievements[indices[index]].points : 0; }
bool Achievements_Supported(unsigned index) {
    if (!loaded || index>=item_count) return false;
    rc_trigger_t *t=rc_runtime_get_achievement(&runtime,game->achievements[indices[index]].id);
    return Achievements_Unlocked(index) || (t && t->state!=RC_TRIGGER_STATE_DISABLED);
}
bool Achievements_Unlocked(unsigned index) { if (!loaded || index>=item_count) return false; Record *r=record(game->achievements[indices[index]].id,false); return r && r->flags; }
size_t Achievements_ProgressSize(void) { return enabled && loaded ? 16+rc_runtime_progress_size(&runtime,NULL) : 0; }
bool Achievements_SaveProgress(uint8_t *data,size_t size) {
    if (size!=Achievements_ProgressSize() || size<16) return false;
    md5_state_t m; md5_init(&m); md5_append(&m,(const uint8_t*)username,strlen(username)); md5_finish(&m,data);
    return rc_runtime_serialize_progress_sized(data+16,size-16,&runtime,NULL)==RC_OK;
}
bool Achievements_LoadProgress(const uint8_t *data,size_t size) {
    if (!enabled || !loaded) return !size;
    uint8_t identity[16]; md5_state_t m; md5_init(&m); md5_append(&m,(const uint8_t*)username,strlen(username)); md5_finish(&m,identity);
    bool okay=size>=16 && !memcmp(identity,data,16) && rc_runtime_deserialize_progress_sized(&runtime,data+16,size-16,NULL)==RC_OK;
    if (!okay) rc_runtime_reset(&runtime);
    for (unsigned i=0;i<record_count;++i) if (records[i].flags) rc_runtime_deactivate_achievement(&runtime,records[i].id);
    return okay;
}
void Achievements_Close(void) {
    NetworkSwitch_Close(); if (loaded) { rc_runtime_destroy(&runtime); rc_api_destroy_fetch_game_data_response(game); free(game); }
    loaded=false; stage=0;
}
