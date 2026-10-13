/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <switch.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "network_switch.h"
#include "network_socket_result.h"
static Thread thread;
static Mutex mutex;
static CondVar condition;
static bool started,running;
static unsigned state;
static char *request_url,*request_post,*response_data;
static int response_status;
static size_t response_size;
size_t NetworkSwitch_ResponseSize(void) { return response_size; }
static char last_error[64];
const char *NetworkSwitch_Error(void) { return last_error; }
static void report(const char *step,int code,int system_error) {
    snprintf(last_error,sizeof(last_error),"%s %08X",step,(unsigned)code);
    mkdir("logs",0777);
    FILE *f=fopen("logs/retroachievements-network.log","a");
    if (f) { fprintf(f,"stage=%s result=%08X errno=%d\n",step,(unsigned)code,system_error); fclose(f); }
}
static int request_ex(const char *url,const char *post,char **response,size_t *size_out,size_t response_limit,char *location) {
    /* Credentials only cross verified TLS; redirects and plaintext are rejected. */
    const char *prefix="https://";
    if (strncmp(url,prefix,8)) return -1;
    const char *path=strchr(url+8,'/'); if (!path) return -1;
    char host[128]; size_t host_length=path-(url+8);
    if (!host_length || host_length>=sizeof(host)) return -1;
    memcpy(host,url+8,host_length); host[host_length]=0;
    struct addrinfo hints={0},*addresses=NULL; hints.ai_socktype=SOCK_STREAM;
    hints.ai_family=AF_INET;
    int dns=getaddrinfo(host,"443",&hints,&addresses);
    if (dns) { report("DNS",dns,errno); return -2; }
    int fd=-1;
    for (struct addrinfo *a=addresses;a;a=a->ai_next) {
        fd=socket(a->ai_family,a->ai_socktype,a->ai_protocol); if (fd<0) continue;
        int flags=fcntl(fd,F_GETFL,0); fcntl(fd,F_SETFL,flags|O_NONBLOCK);
        int result=connect(fd,a->ai_addr,a->ai_addrlen);
        if (result<0 && errno==EINPROGRESS) {
            fd_set write_set; FD_ZERO(&write_set); FD_SET(fd,&write_set);
            struct timeval timeout={8,0}; result=select(fd+1,NULL,&write_set,NULL,&timeout);
            int error=0; socklen_t length=sizeof(error);
            if (result>0 && !getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length) && !error) result=0;
            else result=-1;
        }
        if (!result) { fcntl(fd,F_SETFL,flags); break; }
        close(fd); fd=-1;
    }
    freeaddrinfo(addresses); if (fd<0) { report("TCP CONNECT",-3,errno); return -3; }
    SslContext context={0}; SslConnection connection={0}; int status=-4;
    const char *step="TLS CONTEXT";
    Result rc=sslCreateContext(&context,SslVersion_TlsV12);
    if (R_SUCCEEDED(rc)) { step="TLS CONNECTION"; rc=sslContextCreateConnection(&context,&connection); }
    if (R_SUCCEEDED(rc)) { step="TLS SOCKET OPTION"; rc=sslConnectionSetOption(&connection,SslOptionType_DoNotCloseSocket,true); }
    if (R_SUCCEEDED(rc)) {
        step="TLS SOCKET ATTACH";
        int attached=socketSslConnectionSetSocketDescriptor(&connection,fd),attach_error=errno;
        if (!NetworkSocket_AttachSucceeded(attached,attach_error)) {
            rc=socketGetLastResult();
            if (R_SUCCEEDED(rc)) rc=MAKERESULT(Module_Libnx,LibnxError_BadInput);
        } else if (attached>=0 && attached!=fd) {
            /* The wrapper allocated a replacement descriptor for the socket. */
            close(fd); fd=attached;
        }
    }
    if (R_SUCCEEDED(rc)) { step="TLS HOST NAME"; rc=sslConnectionSetHostName(&connection,host,strlen(host)+1); }
    if (R_SUCCEEDED(rc)) { step="TLS VERIFY OPTION"; rc=sslConnectionSetVerifyOption(&connection,SslVerifyOption_PeerCa|SslVerifyOption_HostName|SslVerifyOption_DateCheck); }
    if (R_SUCCEEDED(rc)) { step="TLS TIMEOUT"; rc=sslConnectionSetIoTimeout(&connection,8000); }
    u32 cert_size=0,cert_count=0;
    if (R_SUCCEEDED(rc)) { step="TLS HANDSHAKE"; rc=sslConnectionDoHandshake(&connection,&cert_size,&cert_count,NULL,0); }
    if (R_FAILED(rc)) report(step,rc,errno);
    char *message=NULL,*raw=NULL;
    if (R_SUCCEEDED(rc)) {
        size_t length=post ? strlen(post) : 0,capacity=strlen(path)+length+512;
        message=malloc(capacity);
        if (!message) rc=1;
        else {
            int size=snprintf(message,capacity,"%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: MZMNX/0.1 rcheevos\r\nAccept: application/json\r\nConnection: close\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %lu\r\n\r\n%s",post ? "POST" : "GET",path,host,(unsigned long)length,post ? post : "");
            unsigned sent=0;
            while (R_SUCCEEDED(rc) && sent<(unsigned)size) {
                u32 written=0; rc=sslConnectionWrite(&connection,message+sent,size-sent,&written);
                if (!written) { rc=1; break; } sent+=written;
            }
            memset(message,0,capacity); free(message); message=NULL;
        }
    }
    if (R_SUCCEEDED(rc)) {
        size_t capacity=response_limit,used=0; raw=malloc(capacity+1);
        if (raw) {
            while (used<capacity) {
                u32 received=0; rc=sslConnectionRead(&connection,raw+used,capacity-used,&received);
                if (R_FAILED(rc)) { report("TLS READ",rc,errno); break; }
                if (!received) break;
                used+=received;
            }
            raw[used]=0;
            char *body=strstr(raw,"\r\n\r\n");
            if (body && location && sscanf(raw,"HTTP/%*s %d",&status)==1 && (status==301 || status==302 || status==303 || status==307 || status==308)) {
                for(char *line=raw;line<body;) {
                    char *end=strstr(line,"\r\n");if(!end)break;
                    if(!strncasecmp(line,"Location:",9)) {
                        char *value=line+9;while(value<end && *value==' ')++value;
                        size_t length=end-value;if(length<2048){memcpy(location,value,length);location[length]=0;}
                        break;
                    }
                    line=end+2;
                }
            }
            if (body && sscanf(raw,"HTTP/%*s %d",&status)==1 && status==200 && used<capacity) {
                *body=0; body+=4; size_t body_length=used-(body-raw);
                /* Decode chunked HTTP bodies without assuming a single TLS read. */
                if (strstr(raw,"chunked") || strstr(raw,"Chunked")) {
                    char *cursor=body,*end=raw+used,*out=raw; bool good=true;
                    while (cursor<end) {
                        char *line_end=strstr(cursor,"\r\n"); if (!line_end) { good=false; break; }
                        char *number_end=NULL; unsigned long chunk=strtoul(cursor,&number_end,16);
                        if (number_end==cursor || number_end>line_end) { good=false; break; }
                        cursor=line_end+2; if (!chunk) break;
                        if ((size_t)(end-cursor)<chunk+2) { good=false; break; }
                        memmove(out,cursor,chunk); out+=chunk; cursor+=chunk+2;
                    }
                    if (good) { *size_out=(size_t)(out-raw); *out=0; *response=raw; raw=NULL; }
                    else status=-5;
                } else { *size_out=body_length; memmove(raw,body,body_length); raw[body_length]=0; *response=raw; raw=NULL; }
            } else if (status==200) status=-5;
        } else status=-6;
    }
    if (status!=200 && R_SUCCEEDED(rc) && !(location && location[0])) report("HTTP RESPONSE",status,0);
    if (R_FAILED(rc) && status!=-4) report("TLS TRANSFER",rc,errno);
    free(raw);
    if (serviceIsActive(&connection.s)) sslConnectionClose(&connection);
    if (serviceIsActive(&context.s)) sslContextClose(&context);
    close(fd); return status;
}
static int request(const char *url,const char *post,char **response,size_t *size_out) {
    return request_ex(url,post,response,size_out,1048576,NULL);
}
static bool public_host(const char *url) {
    static const char *hosts[]={"api.github.com","github.com","release-assets.githubusercontent.com","objects.githubusercontent.com","raw.githubusercontent.com"};
    if(strncmp(url,"https://",8))return false;
    const char *end=strchr(url+8,'/');if(!end)return false;
    for(unsigned i=0;i<sizeof(hosts)/sizeof(hosts[0]);++i)
        if(strlen(hosts[i])==(size_t)(end-url-8) && !strncmp(url+8,hosts[i],end-url-8))return true;
    return false;
}
int NetworkSwitch_PublicGet(const char *url,char **response,size_t *size,size_t limit) {
    char current[2048],location[2048];*response=NULL;*size=0;
    if(strlen(url)>=sizeof(current) || !limit || limit>16777216)return -1;
    strcpy(current,url);
    for(unsigned i=0;i<6;++i) {
        if(!public_host(current))return -1;
        location[0]=0;int result=request_ex(current,NULL,response,size,limit,location);
        if(result==200 || !location[0])return result;
        strcpy(current,location);
    }
    return -1;
}
static void worker(void *unused) {
    (void)unused;
    mutexLock(&mutex);
    while (running) {
        while (running && state!=1) condvarWait(&condition,&mutex);
        if (!running) break;
        char *url=request_url,*post=request_post; state=2;
        mutexUnlock(&mutex);
        char *body=NULL; size_t body_size=0; int status=request(url,post,&body,&body_size);
        free(url); if (post) { memset(post,0,strlen(post)); free(post); }
        mutexLock(&mutex); request_url=request_post=NULL;
        response_data=body; response_size=body_size; response_status=status; state=3;
    }
    mutexUnlock(&mutex);
}
bool NetworkSwitch_Init(void) {
    if (started) return true;
    Result init_rc=socketInitializeDefault();
    if (R_FAILED(init_rc)) { report("SOCKET INIT",init_rc,errno); return false; }
    init_rc=sslInitialize(1);
    if (R_FAILED(init_rc)) { report("TLS INIT",init_rc,errno); socketExit(); return false; }
    running=true;
    Result rc=threadCreate(&thread,worker,NULL,NULL,0x18000,0x31,-2);
    if (R_SUCCEEDED(rc)) rc=threadStart(&thread);
    if (R_FAILED(rc)) { running=false; sslExit(); socketExit(); return false; }
    started=true; return true;
}
bool NetworkSwitch_Request(const char *url,const char *post) {
    if (!NetworkSwitch_Init()) return false;
    mutexLock(&mutex);
    if (state) { mutexUnlock(&mutex); return false; }
    request_url=strdup(url); request_post=post ? strdup(post) : NULL;
    if (!request_url || (post && !request_post)) {
        free(request_url); free(request_post); request_url=request_post=NULL;
        mutexUnlock(&mutex); return false;
    }
    state=1; condvarWakeOne(&condition); mutexUnlock(&mutex); return true;
}
bool NetworkSwitch_Poll(char **response,int *status) {
    if (!started) return false;
    mutexLock(&mutex);
    if (state!=3) { mutexUnlock(&mutex); return false; }
    *response=response_data; *status=response_status; response_data=NULL; state=0;
    mutexUnlock(&mutex); return true;
}
void NetworkSwitch_Close(void) {
    if (!started) return;
    mutexLock(&mutex); running=false; condvarWakeOne(&condition); mutexUnlock(&mutex);
    threadWaitForExit(&thread); threadClose(&thread);
    free(request_url); if (request_post) { memset(request_post,0,strlen(request_post)); free(request_post); }
    free(response_data); request_url=request_post=response_data=NULL; state=0;
    sslExit(); socketExit(); started=false;
}
