/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "screen_overlay.h"
#include <stdio.h>
#include <string.h>
static bool load(const char *path) {
    unsigned char data[8]={0}; FILE *file=fopen(path,"rb"); if (!file) return false;
    size_t count=fread(data,1,8,file); fclose(file);
    if (memcmp(data,"NXD",3) || data[4]>2 || data[5]>3) return false;
    if (data[3]==1 && count==7 && data[6]==(data[4]^data[5]^0x5a)) SwitchOverlay_ShowFps(false);
    else if (data[3]==2 && count==8 && data[6]<=1 && data[7]==(data[4]^data[5]^data[6]^0x5a))
        SwitchOverlay_ShowFps(data[6]);
    else return false;
    SwitchOverlay_SetOptions(data[4],data[5]); return true;
}
void SwitchOverlay_LoadOptions(void) { if (!load("mzmnx-display.cfg")) load("mzmnx-display.cfg.bak"); }
void SwitchOverlay_SaveOptions(void) {
    unsigned char layout=SwitchOverlay_Layout(),scaling=SwitchOverlay_Scaling();
    unsigned char fps=SwitchOverlay_FpsVisible();
    unsigned char data[8]={'N','X','D',2,layout,scaling,fps,layout^scaling^fps^0x5a};
    FILE *file=fopen("mzmnx-display.cfg.tmp","wb"); if (!file) return;
    size_t count=fwrite(data,1,8,file); int result=fflush(file);
    if (fclose(file) || result || count!=8) return;
    remove("mzmnx-display.cfg.bak"); rename("mzmnx-display.cfg","mzmnx-display.cfg.bak");
    rename("mzmnx-display.cfg.tmp","mzmnx-display.cfg");
}
