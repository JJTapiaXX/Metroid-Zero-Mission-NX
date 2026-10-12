#pragma once
/* The login of the in-game FTP server (ftp_server_3ds.h), in debug builds and in the install-only
 * server of production builds. A deterrent, not a secret: it ships in the binary and the
 * repository, and the server is only ever on while the owner asked for it. tools/ read it from
 * here; the same pair is written in CLAUDE.md so a session knows it. */
#define FTP_USER "mzm3ds"
#define FTP_PASS "LdHrf1259oikAkPg"
