#if defined(PORT_DEBUG_TOOLS)
#include "ftp_server_3ds.h"

#include "cia_install_3ds.h"
#include "debug_link_3ds.h"
#include "net_3ds.h"

#include <3ds.h>

#include "platform_3ds.h"

extern void Port_DebugLog(const char* msg);
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum {
    FTP_PATH_MAX = 512,
    FTP_LINE_MAX = 600,
    FTP_IO_SIZE = 64 * 1024,
    FTP_POLL_MS = 250,
    FTP_DATA_ACCEPT_MS = 15000,
    FTP_IDLE_MS = 10 * 60 * 1000,
    FTP_PASV_FIRST = FTP_SERVER_PORT + 1,
    FTP_PASV_COUNT = 64,
};

static Thread sThread;
static volatile bool sRun;
static int sListen = -1;
static volatile int sCtrl = -1;
static volatile int sData = -1;
static bool sNetHeld;
static uint32_t sIp;
static volatile unsigned sSent, sReceived;
static volatile bool sConnected;
static char sStatus[48] = "STOPPED";
/* FtpServer_Notice: what the player is told while a new build comes in. */
static const char* volatile sNotice;
static unsigned sNextPasv;
static char* sIo;

/* Lines for the SD log. Port_DebugLog is not thread-safe, so this thread queues them and the main
 * thread writes them (FtpServer_Tick). One producer, one consumer. */
enum { FTP_LOG_LINES = 8, FTP_LOG_LEN = 160 };
static char sLogRing[FTP_LOG_LINES][FTP_LOG_LEN];
static volatile unsigned sLogHead, sLogTail;
static volatile bool sRelaunch;

static void FtpLog(const char* fmt, ...) {
    const unsigned head = sLogHead;
    if (head - sLogTail >= FTP_LOG_LINES) return; /* full: drop */
    va_list args;
    va_start(args, fmt);
    vsnprintf(sLogRing[head % FTP_LOG_LINES], FTP_LOG_LEN, fmt, args);
    va_end(args);
    __atomic_store_n(&sLogHead, head + 1, __ATOMIC_RELEASE);
}

typedef struct Session {
    int ctrl;
    int pasv; /* listening data socket after PASV/EPSV, -1 when none */
    char cwd[FTP_PATH_MAX];
    char rnfr[FTP_PATH_MAX];
    char in[FTP_LINE_MAX * 2];
    unsigned inUsed;
} Session;

/* ---- sockets ---- */

static void CloseSocket(int* fd) {
    if (*fd >= 0) {
        shutdown(*fd, SHUT_RDWR);
        close(*fd);
        *fd = -1;
    }
}

/* Waits until fd is readable (or writable); false on timeout, error or stop. */
static bool WaitFd(int fd, short events, int timeoutMs) {
    int waited = 0;
    while (sRun) {
        struct pollfd p = { .fd = fd, .events = events, .revents = 0 };
        int r = poll(&p, 1, FTP_POLL_MS);
        if (r > 0) return (p.revents & (events | POLLHUP | POLLERR)) != 0;
        if (r < 0) return false;
        waited += FTP_POLL_MS;
        if (timeoutMs > 0 && waited >= timeoutMs) return false;
    }
    return false;
}

static bool SendAll(int fd, const char* data, size_t n) {
    while (n > 0) {
        if (!WaitFd(fd, POLLOUT, 30000)) return false;
        ssize_t w = send(fd, data, n, 0);
        if (w <= 0) return false;
        data += w;
        n -= (size_t)w;
    }
    return true;
}

static bool Reply(Session* s, const char* fmt, ...) {
    char line[FTP_LINE_MAX];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line) - 2, fmt, args);
    va_end(args);
    if (n < 0) return false;
    if (n > (int)sizeof(line) - 3) n = (int)sizeof(line) - 3;
    line[n++] = '\r';
    line[n++] = '\n';
    return SendAll(s->ctrl, line, (size_t)n);
}

/* One command line, CR/LF stripped. False when the client left or we stop. */
static bool ReadLine(Session* s, char* out, unsigned cap) {
    for (;;) {
        for (unsigned i = 0; i < s->inUsed; i++) {
            if (s->in[i] == '\n') {
                unsigned n = i;
                if (n > 0 && s->in[n - 1] == '\r') n--;
                if (n >= cap) n = cap - 1;
                memcpy(out, s->in, n);
                out[n] = 0;
                memmove(s->in, s->in + i + 1, s->inUsed - i - 1);
                s->inUsed -= i + 1;
                return true;
            }
        }
        if (s->inUsed >= sizeof(s->in) - 1) s->inUsed = 0; /* overlong line: drop it */
        if (!WaitFd(s->ctrl, POLLIN, FTP_IDLE_MS)) return false;
        ssize_t r = recv(s->ctrl, s->in + s->inUsed, sizeof(s->in) - 1 - s->inUsed, 0);
        if (r <= 0) return false;
        s->inUsed += (unsigned)r;
    }
}

/* ---- paths: "/" is the root of the SD card ---- */

static void ResolvePath(const Session* s, const char* arg, char* out, unsigned cap) {
    char joined[FTP_PATH_MAX * 2];
    if (!arg || !arg[0]) snprintf(joined, sizeof(joined), "%s", s->cwd);
    else if (arg[0] == '/') snprintf(joined, sizeof(joined), "%s", arg);
    else snprintf(joined, sizeof(joined), "%s/%s", s->cwd, arg);
    /* Normalise: drop empty and "." parts, resolve "..". */
    char parts[64][128];
    int count = 0;
    char* save = NULL;
    for (char* p = strtok_r(joined, "/", &save); p; p = strtok_r(NULL, "/", &save)) {
        if (!strcmp(p, ".")) continue;
        if (!strcmp(p, "..")) {
            if (count > 0) count--;
            continue;
        }
        if (count < 64) snprintf(parts[count++], sizeof(parts[0]), "%s", p);
    }
    unsigned n = 0;
    out[0] = 0;
    for (int i = 0; i < count && n + 1 < cap; i++) n += (unsigned)snprintf(out + n, cap - n, "/%s", parts[i]);
    if (n == 0) snprintf(out, cap, "/");
}

static void RealPath(const char* virtualPath, char* out, unsigned cap) {
    snprintf(out, cap, "sdmc:%s", virtualPath);
}

/* ---- data connection ---- */

static void ClosePasv(Session* s) {
    CloseSocket(&s->pasv);
}

static int OpenPasv(Session* s) {
    ClosePasv(s);
    for (unsigned attempt = 0; attempt < FTP_PASV_COUNT; attempt++) {
        const unsigned port = FTP_PASV_FIRST + (sNextPasv++ % FTP_PASV_COUNT);
        int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (fd < 0) return -1;
        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        struct sockaddr_in addr = { 0 };
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0 && listen(fd, 1) == 0) {
            s->pasv = fd;
            return (int)port;
        }
        close(fd);
    }
    return -1;
}

/* Accepts the client's data connection; -1 when none came. */
static int AcceptData(Session* s) {
    if (s->pasv < 0) return -1;
    int fd = -1;
    if (WaitFd(s->pasv, POLLIN, FTP_DATA_ACCEPT_MS)) {
        struct sockaddr_in peer;
        socklen_t len = sizeof(peer);
        fd = accept(s->pasv, (struct sockaddr*)&peer, &len);
    }
    ClosePasv(s);
    sData = fd;
    return fd;
}

static void CloseData(int* fd) {
    CloseSocket(fd);
    sData = -1;
}

/* ---- commands ---- */

static void FormatListLine(char* out, unsigned cap, const char* name, const struct stat* st, bool mlsd) {
    const bool dir = S_ISDIR(st->st_mode);
    if (mlsd) {
        snprintf(out, cap, "type=%s;size=%llu; %s\r\n", dir ? "dir" : "file", (unsigned long long)st->st_size, name);
        return;
    }
    /* libctru leaves st_mtime at 0: a fixed date reads better than 1970. */
    snprintf(out, cap, "%s 1 3ds 3ds %12llu Jan  1 00:00 %s\r\n", dir ? "drwxrwxrwx" : "-rw-rw-rw-",
             (unsigned long long)st->st_size, name);
}

static void CmdList(Session* s, const char* arg, int kind /* 0 LIST, 1 NLST, 2 MLSD */) {
    /* Clients send options such as "-a" or "-la": not a path. */
    if (arg && arg[0] == '-') {
        const char* space = strchr(arg, ' ');
        arg = space ? space + 1 : "";
    }
    char vpath[FTP_PATH_MAX], rpath[FTP_PATH_MAX + 8];
    ResolvePath(s, arg, vpath, sizeof(vpath));
    RealPath(vpath, rpath, sizeof(rpath));
    DIR* dir = opendir(rpath);
    if (!dir) {
        ClosePasv(s);
        Reply(s, "550 No such directory.");
        return;
    }
    Reply(s, "150 Listing.");
    int data = AcceptData(s);
    if (data < 0) {
        closedir(dir);
        Reply(s, "425 No data connection.");
        return;
    }
    bool ok = true;
    struct dirent* e;
    while (ok && (e = readdir(dir)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char line[FTP_PATH_MAX + 96];
        if (kind == 1) {
            snprintf(line, sizeof(line), "%s\r\n", e->d_name);
        } else {
            char full[FTP_PATH_MAX * 2];
            struct stat st;
            snprintf(full, sizeof(full), "%s/%s", rpath, e->d_name);
            if (stat(full, &st) != 0) memset(&st, 0, sizeof(st));
            if (e->d_type == DT_DIR) st.st_mode = S_IFDIR;
            FormatListLine(line, sizeof(line), e->d_name, &st, kind == 2);
        }
        ok = SendAll(data, line, strlen(line));
    }
    closedir(dir);
    CloseData(&data);
    Reply(s, ok ? "226 Done." : "426 Transfer aborted.");
}

static void CmdRetr(Session* s, const char* arg) {
    char vpath[FTP_PATH_MAX], rpath[FTP_PATH_MAX + 8];
    ResolvePath(s, arg, vpath, sizeof(vpath));
    RealPath(vpath, rpath, sizeof(rpath));
    FILE* f = fopen(rpath, "rb");
    if (!f) {
        ClosePasv(s);
        Reply(s, "550 Cannot open the file.");
        return;
    }
    Reply(s, "150 Sending.");
    int data = AcceptData(s);
    if (data < 0) {
        fclose(f);
        sNotice = NULL;
        Reply(s, "425 No data connection.");
        return;
    }
    bool ok = true;
    size_t n;
    while (ok && (n = fread(sIo, 1, FTP_IO_SIZE, f)) > 0) ok = SendAll(data, sIo, n);
    fclose(f);
    CloseData(&data);
    if (ok) sSent++;
    Reply(s, ok ? "226 Done." : "426 Transfer aborted.");
}

static void CmdStor(Session* s, const char* arg, bool append) {
    char vpath[FTP_PATH_MAX], rpath[FTP_PATH_MAX + 8];
    ResolvePath(s, arg, vpath, sizeof(vpath));
    RealPath(vpath, rpath, sizeof(rpath));
    FILE* f = fopen(rpath, append ? "ab" : "wb");
    if (!f) {
        ClosePasv(s);
        Reply(s, "553 Cannot create the file.");
        return;
    }
    const size_t len = strlen(vpath);
    const bool cia = len > 4 && strcasecmp(vpath + len - 4, ".cia") == 0;
    if (cia) sNotice = "RECEIVING THE INSTALLER...";
    Reply(s, "150 Receiving.");
    int data = AcceptData(s);
    if (data < 0) {
        fclose(f);
        sNotice = NULL;
        Reply(s, "425 No data connection.");
        return;
    }
    bool ok = true;
    for (;;) {
        if (!WaitFd(data, POLLIN, 30000)) {
            ok = false;
            break;
        }
        ssize_t r = recv(data, sIo, FTP_IO_SIZE, 0);
        if (r == 0) break;
        if (r < 0 || fwrite(sIo, 1, (size_t)r, f) != (size_t)r) {
            ok = false;
            break;
        }
    }
    if (fclose(f) != 0) ok = false;
    CloseData(&data);
    if (ok) sReceived++;
    if (cia) sNotice = NULL;
    FtpLog("ftp: received %s%s", vpath, ok ? "" : " (failed)");
    Reply(s, ok ? "226 Done." : "451 Transfer failed.");
}

static void HandleSession(int ctrl) {
    Session* s = calloc(1, sizeof(Session));
    if (!s) return;
    s->ctrl = ctrl;
    s->pasv = -1;
    /* Log in at the SD card's root, like any FTP server: clients take URL
     * paths relative to it. The game's files are in /3ds/Super Metroid 3DS/. */
    snprintf(s->cwd, sizeof(s->cwd), "/");
    Reply(s, "220 Metroid Zero Mission 3DS debug FTP. SD card root; the game's files are in /3ds/Metroid Zero Mission 3DS/debug.");
    char line[FTP_LINE_MAX];
    while (sRun && ReadLine(s, line, sizeof(line))) {
        char* arg = strchr(line, ' ');
        if (arg) *arg++ = 0;
        else arg = line + strlen(line);
        for (char* c = line; *c; c++)
            if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
        char vpath[FTP_PATH_MAX], rpath[FTP_PATH_MAX + 8];
        struct stat st;
        if (!strcmp(line, "USER")) Reply(s, "331 Any password.");
        else if (!strcmp(line, "PASS")) Reply(s, "230 Logged in.");
        else if (!strcmp(line, "SYST")) Reply(s, "215 UNIX Type: L8");
        else if (!strcmp(line, "FEAT")) Reply(s, "211-Features:\r\n SIZE\r\n MDTM\r\n MLSD\r\n EPSV\r\n PASV\r\n UTF8\r\n211 End");
        else if (!strcmp(line, "OPTS") || !strcmp(line, "TYPE") || !strcmp(line, "MODE") || !strcmp(line, "STRU") ||
                 !strcmp(line, "NOOP") || !strcmp(line, "ALLO"))
            Reply(s, "200 OK.");
        else if (!strcmp(line, "PWD") || !strcmp(line, "XPWD")) Reply(s, "257 \"%s\"", s->cwd);
        else if (!strcmp(line, "CWD") || !strcmp(line, "CDUP")) {
            ResolvePath(s, !strcmp(line, "CDUP") ? ".." : arg, vpath, sizeof(vpath));
            RealPath(vpath, rpath, sizeof(rpath));
            DIR* d = opendir(rpath);
            if (d) {
                closedir(d);
                snprintf(s->cwd, sizeof(s->cwd), "%s", vpath);
                Reply(s, "250 OK.");
            } else {
                Reply(s, "550 No such directory.");
            }
        } else if (!strcmp(line, "PASV")) {
            int port = OpenPasv(s);
            if (port < 0) Reply(s, "425 Cannot open a data port.");
            else
                Reply(s, "227 Entering Passive Mode (%lu,%lu,%lu,%lu,%d,%d).", (unsigned long)(sIp & 0xFF),
                      (unsigned long)((sIp >> 8) & 0xFF), (unsigned long)((sIp >> 16) & 0xFF),
                      (unsigned long)((sIp >> 24) & 0xFF), port >> 8, port & 0xFF);
        } else if (!strcmp(line, "EPSV")) {
            int port = OpenPasv(s);
            if (port < 0) Reply(s, "425 Cannot open a data port.");
            else Reply(s, "229 Entering Extended Passive Mode (|||%d|).", port);
        } else if (!strcmp(line, "LIST")) CmdList(s, arg, 0);
        else if (!strcmp(line, "NLST")) CmdList(s, arg, 1);
        else if (!strcmp(line, "MLSD")) CmdList(s, arg, 2);
        else if (!strcmp(line, "RETR")) CmdRetr(s, arg);
        else if (!strcmp(line, "STOR")) CmdStor(s, arg, false);
        else if (!strcmp(line, "APPE")) CmdStor(s, arg, true);
        else if (!strcmp(line, "SIZE") || !strcmp(line, "MDTM")) {
            ResolvePath(s, arg, vpath, sizeof(vpath));
            RealPath(vpath, rpath, sizeof(rpath));
            if (stat(rpath, &st) != 0 || S_ISDIR(st.st_mode)) Reply(s, "550 No such file.");
            else if (line[0] == 'S') Reply(s, "213 %llu", (unsigned long long)st.st_size);
            else Reply(s, "213 20260101000000");
        } else if (!strcmp(line, "DELE") || !strcmp(line, "RMD") || !strcmp(line, "XRMD") || !strcmp(line, "MKD") ||
                   !strcmp(line, "XMKD")) {
            ResolvePath(s, arg, vpath, sizeof(vpath));
            RealPath(vpath, rpath, sizeof(rpath));
            int r = line[0] == 'D' ? remove(rpath) : (strstr(line, "RMD") ? rmdir(rpath) : mkdir(rpath, 0777));
            if (r != 0) Reply(s, "550 Failed.");
            else if (strstr(line, "MKD")) Reply(s, "257 \"%s\" created.", vpath);
            else Reply(s, "250 OK.");
        } else if (!strcmp(line, "RNFR")) {
            ResolvePath(s, arg, s->rnfr, sizeof(s->rnfr));
            Reply(s, "350 Ready for RNTO.");
        } else if (!strcmp(line, "RNTO")) {
            char from[FTP_PATH_MAX + 8];
            RealPath(s->rnfr, from, sizeof(from));
            ResolvePath(s, arg, vpath, sizeof(vpath));
            RealPath(vpath, rpath, sizeof(rpath));
            Reply(s, s->rnfr[0] && rename(from, rpath) == 0 ? "250 Renamed." : "550 Rename failed.");
            s->rnfr[0] = 0;
        } else if (!strcmp(line, "SITE")) {
            /* SITE INSTALL <path>: install a CIA of this game from the SD card
             * and relaunch into it (./build_3ds.sh --install does this). */
            char* sub = arg;
            char* path = strchr(sub, ' ');
            if (path) *path++ = 0;
            for (char* c = sub; *c; c++)
                if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
            if (!strcmp(sub, "INSTALL") && path && *path) {
                ResolvePath(s, path, vpath, sizeof(vpath));
                char error[96];
                sNotice = "INSTALLING - THE GAME WILL RESTART";
                if (CiaInstall_File(vpath, error, sizeof(error))) {
                    FtpLog("ftp: installed %s, relaunching", vpath);
                    Reply(s, "200 Installed %s; relaunching the game.", vpath);
                    sRelaunch = true;
                } else {
                    FtpLog("ftp: install of %s failed: %s", vpath, error);
                    sNotice = NULL;
                    Reply(s, "550 Install failed: %s", error);
                }
            } else if (!strcmp(sub, "INSTALL")) {
                Reply(s, "501 SITE INSTALL <path to a .cia of this game>");
            } else {
                char reply[FTP_LINE_MAX - 8];
                if (DebugLink_Command(sub, path, reply, sizeof(reply))) Reply(s, "%s", reply);
                else Reply(s, "504 Unknown SITE command.");
            }
        } else if (!strcmp(line, "QUIT")) {
            Reply(s, "221 Bye.");
            break;
        } else {
            Reply(s, "502 Not implemented.");
        }
    }
    ClosePasv(s);
    free(s);
}

static void FtpMain(void* argument) {
    (void)argument;
    while (sRun) {
        if (!WaitFd(sListen, POLLIN, 0)) continue;
        struct sockaddr_in peer;
        socklen_t len = sizeof(peer);
        int fd = accept(sListen, (struct sockaddr*)&peer, &len);
        if (fd < 0) continue;
        sCtrl = fd;
        sConnected = true;
        FtpLog("ftp: client %s connected", inet_ntoa(peer.sin_addr));
        HandleSession(fd);
        sConnected = false;
        int ctrl = fd;
        CloseSocket(&ctrl);
        sCtrl = -1;
        FtpLog("ftp: client left");
    }
}

bool FtpServer_Start(void) {
    if (sRun) return true;
    if (!Net3DS_Acquire()) {
        snprintf(sStatus, sizeof(sStatus), "NO NETWORK");
        return false;
    }
    sNetHeld = true;
    if (!Net3DS_WifiConnected()) {
        snprintf(sStatus, sizeof(sStatus), "NO WI-FI");
        Net3DS_Release();
        sNetHeld = false;
        return false;
    }
    sIo = malloc(FTP_IO_SIZE);
    sListen = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sListen >= 0) {
        /* Restarting the server right after stopping it must not find the
         * port still held by the last connection. */
        int yes = 1;
        setsockopt(sListen, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    }
    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(FTP_SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (!sIo || sListen < 0 || bind(sListen, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(sListen, 1) != 0) {
        snprintf(sStatus, sizeof(sStatus), "PORT %d BUSY", FTP_SERVER_PORT);
        CloseSocket(&sListen);
        free(sIo);
        sIo = NULL;
        Net3DS_Release();
        sNetHeld = false;
        return false;
    }
    sIp = (uint32_t)gethostid();
    sSent = sReceived = 0;
    sRun = true;
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    priority = priority + 4 <= 0x3F ? priority + 4 : 0x3F;
    /* Core 1 when the app has a share of it: the game and present threads live on core 0. */
    sThread = threadCreate(FtpMain, NULL, 64 * 1024, priority, Platform3DS_CanUseCore1() ? 1 : 0, false);
    if (!sThread) {
        sRun = false;
        FtpServer_Stop();
        snprintf(sStatus, sizeof(sStatus), "NO THREAD");
        return false;
    }
    {
        char line[64];
        snprintf(line, sizeof(line), "ftp: serving on %s:%d", inet_ntoa((struct in_addr){ .s_addr = sIp }), FTP_SERVER_PORT);
        Port_DebugLog(line);
    }
    return true;
}

const char* FtpServer_Notice(void) {
    return sNotice;
}

void FtpServer_Tick(void) {
    while (sLogTail != __atomic_load_n(&sLogHead, __ATOMIC_ACQUIRE)) {
        Port_DebugLog(sLogRing[sLogTail % FTP_LOG_LINES]);
        __atomic_store_n(&sLogTail, sLogTail + 1, __ATOMIC_RELEASE);
    }
}

bool FtpServer_TakeRelaunch(void) {
    if (!sRelaunch) return false;
    sRelaunch = false;
    return true;
}

void FtpServer_Stop(void) {
    sRun = false;
    /* Wake anything blocked: the thread polls with a short timeout anyway. */
    int fd = sData;
    if (fd >= 0) shutdown(fd, SHUT_RDWR);
    fd = sCtrl;
    if (fd >= 0) shutdown(fd, SHUT_RDWR);
    if (sThread) {
        threadJoin(sThread, 3000000000ULL);
        threadFree(sThread);
        sThread = NULL;
    }
    CloseSocket(&sListen);
    free(sIo);
    sIo = NULL;
    sConnected = false;
    if (sNetHeld) Net3DS_Release();
    sNetHeld = false;
    snprintf(sStatus, sizeof(sStatus), "STOPPED");
}

bool FtpServer_Running(void) {
    return sRun;
}

void FtpServer_Address(char* out, unsigned cap) {
    if (!sRun) {
        snprintf(out, cap, "%s", sStatus);
        return;
    }
    snprintf(out, cap, "%s:%d", inet_ntoa((struct in_addr){ .s_addr = sIp }), FTP_SERVER_PORT);
}

void FtpServer_Stats(unsigned* sent, unsigned* received, bool* connected) {
    if (sent) *sent = sSent;
    if (received) *received = sReceived;
    if (connected) *connected = sConnected;
}

#endif /* PORT_DEBUG_TOOLS */
