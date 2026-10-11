#include <3ds.h>
#include <curl/curl.h>
#include <malloc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_updater_3ds.h"
#include "port_updater_parse.h"
#include "port_paths.h"
#include "net_3ds.h"

#ifndef MZM_PORT_VERSION
#define MZM_PORT_VERSION "v0.0.0"
#endif

#define UPDATER_DEFAULT_URL "https://api.github.com/repos/tinaut1986/mzm/releases?per_page=8"
#define UPDATER_URL_MAX 200
#define UPDATER_CIA_PATH PORT_UPDATE_DIR "/mzm-update.cia"
#define UPDATER_JSON_MAX (192 * 1024)
#define UPDATER_CHUNK (64 * 1024)
#define UPDATER_MAX_REDIRECTS 5
#define UPDATER_STACK (64 * 1024)
#define UPDATER_FILE_BUF (256 * 1024)
#define UPDATER_MSG_MAX 64

typedef enum {
    JOB_CHECK,             /* manual: stop at AVAILABLE */
    JOB_AUTO_CHECK,        /* at boot: same, but raises the install prompt */
    JOB_INSTALL
} UpdaterJob;

static volatile UpdaterState sState = UPDATER_IDLE;
static volatile int sProgress = 0;
static volatile bool sBusy = false;
static volatile bool sKeptCia = false; /* failed install left the CIA on the SD */
static volatile UpdaterPrompt sPrompt = UPDATER_PROMPT_NONE;
static bool sAuto = true;
static bool sBeta = false;
static char sUrlOverride[UPDATER_URL_MAX] = "";
static char sRemoteTag[32] = "";
#define UPDATER_NOTES_MAX 6144
static char sNotes[UPDATER_NOTES_MAX] = ""; /* what's new since this build; under sTextLock */
static char sMessage[96] = "";
static UpdaterRelease sRelease;
static LightLock sTextLock;
static bool sLockReady = false;
static bool sNetUp = false; /* holding the shared socket service (net_3ds.c) */

static void EnsureLock(void) {
    if (!sLockReady) {
        LightLock_Init(&sTextLock);
        sLockReady = true;
    }
}

static void SetMessage(const char* fmt, const char* arg) {
    EnsureLock();
    LightLock_Lock(&sTextLock);
    snprintf(sMessage, sizeof(sMessage), fmt, arg ? arg : "");
    LightLock_Unlock(&sTextLock);
}

static void Fail(const char* msg, Result rc) {
    char buf[96];
    if (rc != 0) {
        snprintf(buf, sizeof(buf), "%s 0x%08lX", msg, (unsigned long)rc);
        SetMessage("%s", buf);
    } else {
        SetMessage("%s", msg);
    }
    sState = UPDATER_ERROR;
}

/* ------------------------------------------------------------------------- */
/* HTTP (libcurl + mbedtls: the console's own TLS cannot talk to GitHub)     */
/* ------------------------------------------------------------------------- */

typedef struct {
    /* Called once the final 200 response is known; total is the
     * Content-Length (0 when unknown). Return false to abort. */
    bool (*begin)(void* user, u32 total);
    bool (*data)(void* user, const u8* buf, u32 size);
    void* user;
} HttpSink;

typedef struct {
    const HttpSink* sink;
    CURL* curl;
    bool started;
    bool aborted;
    u32 got;
} CurlCtx;

static size_t CurlWrite(char* ptr, size_t size, size_t nmemb, void* userdata) {
    CurlCtx* c = (CurlCtx*)userdata;
    size_t n = size * nmemb;

    if (!c->started) {
        long status = 0;
        curl_off_t total = -1;

        curl_easy_getinfo(c->curl, CURLINFO_RESPONSE_CODE, &status);
        if (status != 200) return n; /* body of an error page: ignore */
        curl_easy_getinfo(c->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &total);
        c->started = true;
        if (c->sink->begin && !c->sink->begin(c->sink->user, total > 0 ? (u32)total : 0)) {
            c->aborted = true;
            return 0;
        }
    }
    if (!c->sink->data(c->sink->user, (const u8*)ptr, (u32)n)) {
        c->aborted = true;
        return 0;
    }
    c->got += (u32)n;
    return n;
}

static bool SocketsUp(void) {
    if (!sNetUp) {
        if (!Net3DS_Acquire()) return false;
        sNetUp = true;
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    return true;
}

static void SocketsDown(void) {
    if (sNetUp) {
        curl_global_cleanup();
        Net3DS_Release();
        sNetUp = false;
    }
}

/* GET `url` (following redirects) and feed the body to `sink`. Returns 0 on
 * success, otherwise a negative code and sets the message. */
static int HttpGet(const char* url, const HttpSink* sink) {
    CurlCtx ctx;
    CURL* curl;
    CURLcode cc;
    long status = 0;

    if (!SocketsUp()) {
        Fail("SOCKETS", 0);
        return -1;
    }
    curl = curl_easy_init();
    if (!curl) {
        Fail("CURL INIT", 0);
        return -2;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.sink = sink;
    ctx.curl = curl;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "mzm-3ds-updater/" MZM_PORT_VERSION);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, (long)UPDATER_CHUNK);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    cc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (ctx.aborted) return -3; /* the sink already set the message */
    if (cc != CURLE_OK) {
        char buf[UPDATER_MSG_MAX];
        snprintf(buf, sizeof(buf), "CURL %d %s", (int)cc, curl_easy_strerror(cc));
        SetMessage("%s", buf);
        sState = UPDATER_ERROR;
        return -4;
    }
    if (status != 200) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%ld", status);
        SetMessage("HTTP %s", buf);
        sState = UPDATER_ERROR;
        return -5;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Sinks                                                                     */
/* ------------------------------------------------------------------------- */

typedef struct {
    char* buf;
    u32 len;
} JsonSink;

static bool JsonData(void* user, const u8* data, u32 size) {
    JsonSink* s = (JsonSink*)user;
    u32 room = UPDATER_JSON_MAX - 1 - s->len;
    if (size > room) size = room; /* the release list we want is at the top */
    memcpy(s->buf + s->len, data, size);
    s->len += size;
    s->buf[s->len] = '\0';
    return true;
}

/* The update has to land where the running title is installed: a CIA for a
 * title that lives on NAND is refused when written to SD, and vice versa.
 * Falls back to SD (the usual place) when the query fails, e.g. under the
 * Homebrew Launcher. Also reports the running title's ID (0 if unknown). */
static FS_MediaType InstalledMediaType(u64* outPid) {
    u64 pid = 0;
    u8 media = MEDIATYPE_SD;
    bool registered = false, loaded = false;
    APT_AppletAttr attr;

    if (R_SUCCEEDED(APT_GetAppletInfo(APPID_APPLICATION, &pid, &media, &registered, &loaded, &attr)) &&
        (media == MEDIATYPE_SD || media == MEDIATYPE_NAND)) {
        if (outPid) *outPid = pid;
        return (FS_MediaType)media;
    }
    if (outPid) *outPid = 0;
    return MEDIATYPE_SD;
}

/* The CIA is downloaded to the SD card first and installed from there. The
 * installer rejects a CIA for the title that is running when the two are
 * streamed together, and a whole file on the SD is what lets a failed install
 * be finished by hand with FBI instead of leaving the player with nothing. */
typedef struct {
    FILE* file;
    u32 total;
    u32 written;
} FileSink;

static bool FileBegin(void* user, u32 total) {
    FileSink* s = (FileSink*)user;

    if (total == 0) {
        Fail("NO CONTENT-LENGTH", 0);
        return false;
    }
    s->total = total;
    s->file = fopen(UPDATER_CIA_PATH, "wb");
    if (!s->file) {
        Fail("CANNOT WRITE SD", 0);
        return false;
    }
    /* libcurl hands over at most 16KB per callback; without a big buffer each
     * one becomes its own tiny SD write, which is far slower than the large
     * sequential writes an FTP upload gets. */
    setvbuf(s->file, NULL, _IOFBF, UPDATER_FILE_BUF);
    return true;
}

static bool FileData(void* user, const u8* data, u32 size) {
    FileSink* s = (FileSink*)user;

    if (fwrite(data, 1, size, s->file) != size) {
        Fail("SD FULL?", 0);
        return false;
    }
    s->written += size;
    /* Download is the first 70% of the bar, the install the rest. */
    sProgress = (int)(((u64)s->written * 70) / s->total);
    return true;
}

/* Streams the downloaded file into the installer. `overwrite` picks the
 * install-over-existing-title variant. */
static bool InstallFromFile(FS_MediaType media, bool overwrite) {
    FILE* f = fopen(UPDATER_CIA_PATH, "rb");
    Handle cia;
    Result r;
    u8* buf;
    u32 total, offset = 0;
    char msg[UPDATER_MSG_MAX];

    if (!f) {
        Fail("CANNOT READ SD", 0);
        return false;
    }
    fseek(f, 0, SEEK_END);
    total = (u32)ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (u8*)malloc(UPDATER_CHUNK);
    if (!buf || total == 0) {
        free(buf);
        fclose(f);
        Fail("OUT OF MEMORY", 0);
        return false;
    }

    r = overwrite ? AM_StartCiaInstallOverwrite(&cia, media) : AM_StartCiaInstall(media, &cia);
    if (R_FAILED(r)) {
        snprintf(msg, sizeof(msg), "AM START %s", overwrite ? "OW" : "ST");
        free(buf);
        fclose(f);
        Fail(msg, r);
        return false;
    }

    while (offset < total) {
        size_t n = fread(buf, 1, UPDATER_CHUNK, f);
        u32 wrote = 0;

        if (n == 0) {
            r = -1;
        } else {
            r = FSFILE_Write(cia, &wrote, offset, buf, (u32)n, 0);
        }
        if (R_FAILED(r) || wrote != n) {
            snprintf(msg, sizeof(msg), "AM WRITE %s @%lu", overwrite ? "OW" : "ST", (unsigned long)offset);
            AM_CancelCIAInstall(cia);
            free(buf);
            fclose(f);
            Fail(msg, r);
            return false;
        }
        offset += (u32)n;
        sProgress = 70 + (int)(((u64)offset * 30) / total);
    }
    free(buf);
    fclose(f);

    r = AM_FinishCiaInstall(cia);
    if (R_FAILED(r)) {
        snprintf(msg, sizeof(msg), "AM FINISH %s", overwrite ? "OW" : "ST");
        Fail(msg, r);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------------- */
/* Worker                                                                    */
/* ------------------------------------------------------------------------- */

static const char* ResolveUrl(void) {
    return sUrlOverride[0] ? sUrlOverride : UPDATER_DEFAULT_URL;
}

static bool DoCheck(void) {
    JsonSink js;
    HttpSink sink = { NULL, JsonData, &js };
    UpdaterRelease rel;
    char* notes;

    js.buf = (char*)malloc(UPDATER_JSON_MAX);
    js.len = 0;
    if (!js.buf) {
        Fail("OUT OF MEMORY", 0);
        return false;
    }
    js.buf[0] = '\0';

    sState = UPDATER_CHECKING;
    SetMessage("%s", "");
    if (HttpGet(ResolveUrl(), &sink) != 0) {
        free(js.buf);
        return false;
    }
    if (!Updater_PickRelease(js.buf, sBeta, &rel)) {
        free(js.buf);
        Fail("NO RELEASE FOUND", 0);
        return false;
    }
    /* The notes of every release between this build and the one found (or the
     * latest ones when there is none), read
     * from the same response, so showing them costs no second request. */
    notes = (char*)malloc(UPDATER_NOTES_MAX);
    if (notes) {
        /* Up to date: nothing is newer than this build, so show the latest
         * published releases instead (everything the response carries). */
        if (Updater_CollectNotes(js.buf, sBeta, MZM_PORT_VERSION, MZM_PORT_IS_BETA, notes, UPDATER_NOTES_MAX) == 0)
            Updater_CollectNotes(js.buf, sBeta, "v0.0.0", false, notes, UPDATER_NOTES_MAX);
    }
    free(js.buf);

    EnsureLock();
    LightLock_Lock(&sTextLock);
    snprintf(sRemoteTag, sizeof(sRemoteTag), "%s", rel.tag);
    snprintf(sNotes, sizeof(sNotes), "%s", notes ? notes : "");
    LightLock_Unlock(&sTextLock);
    free(notes);

    if (!Updater_IsNewerBuild(MZM_PORT_VERSION, MZM_PORT_IS_BETA, rel.tag, rel.prerelease)) {
        sState = UPDATER_UP_TO_DATE;
        return false;
    }
    sRelease = rel;
    sState = UPDATER_AVAILABLE;
    return true;
}

static void DoInstall(void) {
    FileSink fs;
    HttpSink sink = { FileBegin, FileData, &fs };
    FS_MediaType media;
    u64 pid = 0;
    Result r;
    int rc;

    memset(&fs, 0, sizeof(fs));
    sProgress = 0;
    sKeptCia = false;
    sState = UPDATER_DOWNLOADING;
    SetMessage("%s", "");

    rc = HttpGet(sRelease.cia_url, &sink);
    if (fs.file) fclose(fs.file);
    if (rc != 0) {
        remove(UPDATER_CIA_PATH);
        if (sState != UPDATER_ERROR) sState = UPDATER_ERROR;
        return;
    }

    r = amInit();
    if (R_FAILED(r)) {
        Fail("AM INIT (am:net?)", r);
        return;
    }
    media = InstalledMediaType(&pid);

    if (!InstallFromFile(media, true)) {
        /* Replacing the running title in place was refused. Remove the old
         * install (this process keeps running from memory) and install the
         * downloaded CIA fresh. If even that fails the file stays on the SD
         * so FBI can finish the job. */
        sState = UPDATER_DOWNLOADING;
        if (pid == 0 || R_FAILED(AM_DeleteTitle(media, pid)) || !InstallFromFile(media, false)) {
            sKeptCia = true;
            sState = UPDATER_ERROR;
            amExit();
            return;
        }
    }
    amExit();
    remove(UPDATER_CIA_PATH);
    sProgress = 100;
    sState = UPDATER_INSTALLED;
    sPrompt = UPDATER_PROMPT_ASK_RESTART;
}

static void WorkerMain(void* arg) {
    UpdaterJob job = (UpdaterJob)(uintptr_t)arg;

    if (job == JOB_INSTALL) {
        DoInstall();
    } else if (DoCheck() && job == JOB_AUTO_CHECK) {
        sPrompt = UPDATER_PROMPT_ASK_INSTALL;
    }
    /* Only an install the player is watching reports its failure in the
     * prompt; a failed silent check (no Wi-Fi) must not pop anything up. */
    if (sState == UPDATER_ERROR && sPrompt == UPDATER_PROMPT_PROGRESS) {
        sPrompt = UPDATER_PROMPT_ERROR;
    }
    SocketsDown();
    sBusy = false;
}

static void StartJob(UpdaterJob job) {
    if (sBusy) return;
    sBusy = true;
    if (!threadCreate(WorkerMain, (void*)(uintptr_t)job, UPDATER_STACK, 0x30, -2, true)) {
        sBusy = false;
        Fail("THREAD", 0);
    }
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

void Port_Updater_Init(void) {
    EnsureLock();
    if (sAuto) StartJob(JOB_AUTO_CHECK);
}

void Port_Updater_CheckNow(void) { StartJob(JOB_CHECK); }

void Port_Updater_Install(void) {
    if (sState == UPDATER_AVAILABLE) StartJob(JOB_INSTALL);
}

void Port_Updater_Restart(void) {
    extern void Platform3DS_RequestQuit(void);
    aptSetChainloaderToSelf();
    Platform3DS_RequestQuit();
}

UpdaterState Port_Updater_GetState(void) { return sState; }
UpdaterPrompt Port_Updater_GetPrompt(void) { return sPrompt; }
bool Port_Updater_KeptCia(void) { return sKeptCia; }

void Port_Updater_AnswerPrompt(bool yes) {
    switch (sPrompt) {
        case UPDATER_PROMPT_ASK_INSTALL:
            if (yes) {
                sPrompt = UPDATER_PROMPT_PROGRESS;
                Port_Updater_Install();
            } else {
                sPrompt = UPDATER_PROMPT_NONE;
            }
            break;
        case UPDATER_PROMPT_ASK_RESTART:
            if (yes) {
                Port_Updater_Restart(); /* the prompt goes away with the process */
            } else {
                sPrompt = UPDATER_PROMPT_NONE;
            }
            break;
        case UPDATER_PROMPT_ERROR:
            sPrompt = UPDATER_PROMPT_NONE;
            break;
        default:
            break;
    }
}
int Port_Updater_GetProgress(void) { return sProgress; }

const char* Port_Updater_GetRemoteTag(void) { return sRemoteTag; }
const char* Port_Updater_GetMessage(void) { return sMessage; }

size_t Port_Updater_CopyNotes(char* out, size_t cap) {
    size_t n;

    if (!out || cap == 0) return 0;
    EnsureLock();
    LightLock_Lock(&sTextLock);
    snprintf(out, cap, "%s", sNotes);
    LightLock_Unlock(&sTextLock);
    n = strlen(out);
    return n;
}

bool Port_Updater_GetAuto(void) { return sAuto; }
void Port_Updater_SetAuto(bool enabled) { sAuto = enabled; }
bool Port_Updater_GetBeta(void) { return sBeta; }
void Port_Updater_SetBeta(bool enabled) { sBeta = enabled; }

const char* Port_Updater_GetUrlOverride(void) { return sUrlOverride; }
void Port_Updater_SetUrlOverride(const char* url) {
    snprintf(sUrlOverride, sizeof(sUrlOverride), "%s", url ? url : "");
}
