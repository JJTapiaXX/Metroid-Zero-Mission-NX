#ifndef PORT_PATHS_H
#define PORT_PATHS_H

/* Every file the 3DS port writes lives under one folder, so the SD root and
 * /3ds stay clean:
 *
 *   sdmc:/3ds/Metroid Zero Mission 3DS/
 *     *.gba, mzm3ds.ini, save data   (the game folder itself)
 *     states/                        save states, warp point
 *     debug/                         logs (debug, RetroAchievements), perf/recorder/
 *                                    dump captures, atlas dumps
 *     update/                        CIA downloaded by the self-updater
 *
 * Port_Paths_Ensure() creates the folders; call it as early as possible,
 * before the first Port_DebugLog(). */

#ifdef PLATFORM_SWITCH
#define PORT_APP_DIR "sdmc:/switch/mzm"
#elif defined(PLATFORM_LINUX)
#define PORT_APP_DIR "/tmp/mzm"
#else
#define PORT_APP_DIR "sdmc:/3ds/Metroid Zero Mission 3DS"
#endif

#define PORT_STATES_DIR PORT_APP_DIR "/states"
#define PORT_DEBUG_DIR  PORT_APP_DIR "/debug"
#define PORT_UPDATE_DIR PORT_APP_DIR "/update"

/* Creates PORT_APP_DIR and its subfolders. Returns false only if the game
 * folder itself could not be created. */
int Port_Paths_Ensure(void);

#endif /* PORT_PATHS_H */
