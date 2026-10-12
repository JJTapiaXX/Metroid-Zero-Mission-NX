#pragma once
// A small FTP server inside the game, for DEBUG_TOOLS builds (DEBUG -> HERRAMIENTAS -> FTP
// SERVER): fetch logs, dumps and recordings, or drop files on the SD card, while the game keeps
// running. No FBI or ftpd round trip.
//
// Anonymous (any user and password), one client at a time, passive mode only, the whole SD card
// as "/". Commands: USER PASS SYST FEAT TYPE MODE STRU PWD CWD CDUP PASV EPSV LIST NLST MLSD
// RETR STOR APPE DELE MKD RMD RNFR RNTO SIZE MDTM NOOP QUIT OPTS. Enough for FileZilla, WinSCP,
// curl and lftp.
//
// SITE INSTALL <path>: installs that CIA (it must be this game's title ID) over the running
// game and relaunches into the new build. With curl:
//   curl -T game.cia "ftp://IP:5000/3ds/Metroid%20Zero%20Mission%203DS/update/dev.cia"
//        -Q "-SITE INSTALL /3ds/Metroid Zero Mission 3DS/update/dev.cia"   (the "-": after the upload)
// (tools/build_3ds.py --install does both). The bottom screen says so meanwhile
// (FtpServer_Notice): an install never happens without a warning.
//
// Other SITE commands are the remote control, debug_link_3ds.h.
//
// Its thread runs at a lower priority than the game and blocks on the network, so it costs
// nothing while idle.
#include <stdbool.h>
#include <stdint.h>

enum { FTP_SERVER_PORT = 5000 };

bool FtpServer_Start(void);
/* Production builds: the install-only server, started when dev-ftp.txt is in the data folder. Only
 * /3ds/Metroid Zero Mission 3DS/update is reachable, the remote control (SITE) is off, and the
 * bottom screen shows the address with a CANCEL button (FtpServer_Cancel). Not started again
 * after a cancel: the player has to leave the game and open it again. */
bool FtpServer_StartInstallOnly(void);
bool FtpServer_InstallOnly(void); /* serving in that mode right now */
void FtpServer_Cancel(void);
void FtpServer_Stop(void);
bool FtpServer_Running(void);
// "192.168.1.50:5000", or a short reason when it is not serving.
void FtpServer_Address(char *out, unsigned cap);
// Files sent and received since start, and whether a client is connected.
void FtpServer_Stats(unsigned *sent, unsigned *received, bool *connected);
// While a .cia is being received or SITE INSTALL runs, the line to show the player (the
// bottom screen's toast); NULL otherwise.
const char *FtpServer_Notice(void);
// Main thread, once per frame: writes what the server thread has to say into the SD log.
void FtpServer_Tick(void);
// True once, after a SITE INSTALL succeeded: the main loop jumps into the new build.
bool FtpServer_TakeRelaunch(void);
