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

static int fatal(const char *message) {
    consoleInit(NULL);
    printf("Metroid: Zero Mission NX 1.0\n\n%s\n\nPress Plus to return to hbmenu.\n", message);
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
    mkdir("sdmc:/switch/mzm", 0777);
    if (chdir("sdmc:/switch/mzm") != 0) return fatal("Cannot open sdmc:/switch/mzm");
    Diagnostics_Init();
    Diagnostics_Event("loading ROM");
    if (!GameSwitch_Load("mzm.gba")) return fatal("Cannot load mzm.gba. Put your clean USA/EUR/JPN ROM in sdmc:/switch/mzm/mzm.gba.");
    if (!PlatformSwitch_Init()) return fatal("Display initialization failed.");
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
