#pragma once
// The socket service, shared by the updater and the debug FTP server. socInit can run only once
// at a time, so both take it through this reference count; the last release shuts it down.
// Thread-safe.
#include <stdbool.h>

// Brings up SOC (and AC, for the Wi-Fi status) on the first acquire. False when SOC fails.
bool Net3DS_Acquire(void);
void Net3DS_Release(void);
// True when the console is connected to a Wi-Fi network (needs an acquired service).
bool Net3DS_WifiConnected(void);
