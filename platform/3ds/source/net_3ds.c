#include "net_3ds.h"

#include <3ds.h>
#include <malloc.h>
#include <stdlib.h>

enum { kSocBufferSize = 1024 * 1024 };   // also bounds the TCP window

static LightLock g_lock = 1;   // LightLock's unlocked value: no init call needed
static int g_refs;
static void *g_soc_buffer;
static bool g_ac_ready;

bool Net3DS_Acquire(void) {
  LightLock_Lock(&g_lock);
  bool ok = true;
  if (g_refs == 0) {
    // AC only tells whether Wi-Fi is up; the service works without it (WifiConnected then asks
    // the shared memory's signal strength).
    g_ac_ready = R_SUCCEEDED(acInit());
    g_soc_buffer = memalign(0x1000, kSocBufferSize);
    if (!g_soc_buffer || R_FAILED(socInit(g_soc_buffer, kSocBufferSize))) {
      free(g_soc_buffer);
      g_soc_buffer = NULL;
      if (g_ac_ready) acExit();
      g_ac_ready = false;
      ok = false;
    }
  }
  if (ok) g_refs++;
  LightLock_Unlock(&g_lock);
  return ok;
}

void Net3DS_Release(void) {
  LightLock_Lock(&g_lock);
  if (g_refs > 0 && --g_refs == 0) {
    socExit();
    free(g_soc_buffer);
    g_soc_buffer = NULL;
    if (g_ac_ready) acExit();
    g_ac_ready = false;
  }
  LightLock_Unlock(&g_lock);
}

bool Net3DS_WifiConnected(void) {
  u32 wifi = 0;
  if (g_ac_ready && R_SUCCEEDED(ACU_GetWifiStatus(&wifi))) return wifi != 0;
  return osGetWifiStrength() > 0;
}
