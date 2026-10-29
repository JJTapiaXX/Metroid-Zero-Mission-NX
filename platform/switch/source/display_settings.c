/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "screen_overlay.h"
#include <stdio.h>
#include <string.h>
static bool load(const char *path) {
    unsigned char data[16]={0}; FILE *file=fopen(path,"rb"); if (!file) return false;
    size_t count=fread(data,1,sizeof(data),file); fclose(file);
    if (memcmp(data,"NXD",3) || data[4]>5 || data[5]>4) return false;
    if (data[3]==1 && count==7 && data[6]==(data[4]^data[5]^0x5a)) SwitchOverlay_ShowFps(false);
    else if (data[3]==2 && count==8 && data[6]<=1 && data[7]==(data[4]^data[5]^data[6]^0x5a))
        SwitchOverlay_ShowFps(data[6]);
    else if (data[3]==3 && count==16 && data[6]<=1 && data[7]<=3 && data[14]<=1) {
        unsigned char checksum=0x5a;
        for (unsigned i=0;i<15;++i) checksum^=data[i];
        int x=data[8]|data[9]<<8,y=data[10]|data[11]<<8,width=data[12]|data[13]<<8;
        if (data[15]!=checksum || width<256 || width>640 || x<4 || x>1276-width || y<4 || y>716-width*3/4) return false;
        SwitchOverlay_SetCornerPlacement(data[14],x,y,width);
        SwitchOverlay_ShowFps(data[6]); SwitchOverlay_SetLcd(data[7]);
    } else return false;
    if(data[3]<3) { SwitchOverlay_SetCornerPlacement(false,4,4,312); SwitchOverlay_SetLcd(0); }
    SwitchOverlay_SetOptions(data[4],data[5]); return true;
}
void SwitchOverlay_LoadOptions(void) { if (!load("mzmnx-display.cfg")) load("mzmnx-display.cfg.bak"); }
void SwitchOverlay_SaveOptions(void) {
    unsigned char layout=SwitchOverlay_Layout(),scaling=SwitchOverlay_Scaling();
    unsigned char fps=SwitchOverlay_FpsVisible();
    int x,y,width;
    bool custom=SwitchOverlay_CornerPlacement(&x,&y,&width);
    if(!custom) { x=4; y=4; }
    unsigned char data[16]={'N','X','D',3,layout,scaling,fps,SwitchOverlay_Lcd(),
        x&255,x>>8,y&255,y>>8,width&255,width>>8,custom,0};
    unsigned char checksum=0x5a;
    for(unsigned i=0;i<15;++i) checksum^=data[i];
    data[15]=checksum;
    FILE *file=fopen("mzmnx-display.cfg.tmp","wb"); if (!file) return;
    size_t count=fwrite(data,1,sizeof(data),file); int result=fflush(file);
    if (fclose(file) || result || count!=sizeof(data)) return;
    remove("mzmnx-display.cfg.bak"); rename("mzmnx-display.cfg","mzmnx-display.cfg.bak");
    rename("mzmnx-display.cfg.tmp","mzmnx-display.cfg");
}
