#include "cia_install_3ds.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>

enum { kCiaIoSize = 128 * 1024 };

// The running title's ID and media (APT knows the application the system started).
static bool RunningTitle(u64 *pid, FS_MediaType *media) {
  u8 m = MEDIATYPE_SD;
  bool registered = false, loaded = false;
  APT_AppletAttr attr;
  if (R_FAILED(APT_GetAppletInfo(APPID_APPLICATION, pid, &m, &registered, &loaded, &attr)) || *pid == 0) return false;
  *media = (m == MEDIATYPE_NAND) ? MEDIATYPE_NAND : MEDIATYPE_SD;
  return true;
}

static bool EnoughSpace(u64 required) {
  FS_ArchiveResource r;
  return R_SUCCEEDED(FSUSER_GetSdmcArchiveResource(&r)) && (u64)r.freeClusters * r.clusterSize >= required;
}

bool CiaInstall_File(const char *sdPath, char *error, unsigned errorCap) {
  u64 title = 0;
  FS_MediaType media = MEDIATYPE_SD;
  if (!RunningTitle(&title, &media)) {
    snprintf(error, errorCap, "not running as an installed title");
    return false;
  }
  Result rc = amInit();
  if (R_FAILED(rc)) {
    snprintf(error, errorCap, "AM unavailable (%08lx)", (unsigned long)rc);
    return false;
  }
  Handle input = 0, output = 0;
  bool started = false, ok = false;
  u64 size = 0, required = 0;
  AM_TitleInfo info;
  unsigned char *data = malloc(kCiaIoSize);
  snprintf(error, errorCap, "out of memory");
  if (!data) goto done;
  rc = FSUSER_OpenFileDirectly(&input, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, sdPath),
                               FS_OPEN_READ, 0);
  snprintf(error, errorCap, "cannot open %s", sdPath);
  if (R_FAILED(rc) || R_FAILED(FSFILE_GetSize(input, &size))) goto done;
  rc = AM_GetCiaFileInfo(media, &info, input);
  snprintf(error, errorCap, "not a CIA of this game (title %016llx)", R_SUCCEEDED(rc) ? info.titleID : 0ull);
  if (R_FAILED(rc) || info.titleID != title) goto done;
  rc = AM_GetCiaRequiredSpace(&required, media, input);
  snprintf(error, errorCap, "not enough SD space");
  if (R_FAILED(rc) || !EnoughSpace(required + 4 * 1024 * 1024ull)) goto done;
  // Overwrite through AM; the installed title is never deleted first.
  rc = AM_StartCiaInstallOverwrite(&output, media);
  snprintf(error, errorCap, "install start failed (%08lx)", (unsigned long)rc);
  if (R_FAILED(rc)) goto done;
  started = true;
  for (u64 offset = 0; offset < size;) {
    u32 read = 0, wrote = 0, want = size - offset > kCiaIoSize ? kCiaIoSize : (u32)(size - offset);
    rc = FSFILE_Read(input, &read, offset, data, want);
    snprintf(error, errorCap, "SD read failed (%08lx)", (unsigned long)rc);
    if (R_FAILED(rc) || read != want) goto done;
    rc = FSFILE_Write(output, &wrote, offset, data, read, 0);
    snprintf(error, errorCap, "install write failed (%08lx)", (unsigned long)rc);
    if (R_FAILED(rc) || wrote != read) goto done;
    offset += read;
  }
  rc = AM_FinishCiaInstall(output);
  started = false;
  ok = R_SUCCEEDED(rc);
  snprintf(error, errorCap, ok ? "" : "install finish failed (%08lx)", (unsigned long)rc);
done:
  if (started) AM_CancelCIAInstall(output);
  if (input) FSFILE_Close(input);
  free(data);
  amExit();
  return ok;
}

bool CiaInstall_JumpToSelf(void) {
  u64 title = 0;
  FS_MediaType media = MEDIATYPE_SD;
  u8 param[0x300] = { 0 };
  u8 hmac[0x20] = { 0 };
  if (!RunningTitle(&title, &media)) return false;
  if (R_FAILED(APT_PrepareToDoApplicationJump(0, title, media))) return false;
  return R_SUCCEEDED(APT_DoApplicationJump(param, sizeof(param), hmac));
}
