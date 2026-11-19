/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <switch.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include "platform_switch.h"
#include "game_switch.h"
#include "audio_switch.h"
#include "switch_ui.h"
#include "diagnostics.h"
#include "updater_switch.h"
#ifdef PLATFORM_SWITCH2
#include "presenter_ounce.h"
#endif

#ifndef MZM_DATA_PATH
#define MZM_DATA_PATH "sdmc:/switch/mzm"
#endif

static int fatal(const char *message) {
    consoleInit(NULL);
    printf("Metroid: Zero Mission NX 1.4\n\n%s\n\nPress Plus to return to hbmenu.\n", message);
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 1;
}

int main(int argc, char **argv) {
    Updater_Init(argc>0 ? argv[0] : NULL);
    mkdir(MZM_DATA_PATH, 0777);
    if (chdir(MZM_DATA_PATH) != 0) return fatal("Cannot open " MZM_DATA_PATH);
    Diagnostics_Init();
#ifdef PLATFORM_SWITCH2
    /* Mesa/OunceVK writes diagnostics to stderr. Keep them alongside the
     * existing runtime and exception logs for hardware bring-up. */
    freopen("logs/ouncevk.log", "w", stderr);
    setvbuf(stderr, NULL, _IOLBF, 0);
    Diagnostics_Event("platform=Switch 2 Kaihen, graphics=OunceVK");
#endif
    Diagnostics_Event("loading ROM");
    if (!GameSwitch_Load("mzm.gba")) return fatal("Cannot load mzm.gba. Put your clean USA/EUR/JPN ROM in " MZM_DATA_PATH "/mzm.gba.");
    if (!PlatformSwitch_Init()) return fatal("Display initialization failed.");
#ifdef PLATFORM_SWITCH2
    if (!PresenterOunce_Warmup(2000)) {
        PlatformSwitch_Exit();Diagnostics_Close();return 1;
    }
#endif
    bool audio_ok=AudioSwitch_Init() && AudioSwitch_StartWorker();
    SwitchUI_SetAudioAvailable(audio_ok);
    GameSwitch_Run();
    Updater_Close();
    AudioSwitch_StopWorker();
    GameSwitch_Shutdown();
    AudioSwitch_Exit();
    PlatformSwitch_Exit();
    Diagnostics_Close();
    return 0;
}
