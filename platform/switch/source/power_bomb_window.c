/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "port_rom.h"
#include "port_gba_mem.h"
#include "structs/haze.h"
#include "structs/game_state.h"
#include "data/haze_data.h"
#include "constants/game_state.h"
static int window_left[192],window_right[192];
extern int SwitchWide_CenterShift(void);
void SwitchPowerBomb_NoteWindow(int radius,int x,int y) {
    if(radius<0 || radius>160) return;
    const s16 *shape=GBA_RESOLVE(sHaze_PowerBomb_WindowValuesPointers[radius]);
    for(int line=-16;line<176;++line) {
        int slice=line-y+radius;
        window_left[line+16]=window_right[line+16]=0;
        if(slice>=0 && slice<radius*2+1) {
            window_left[line+16]=x+shape[slice*2+1]*2;
            window_right[line+16]=x+shape[slice*2]*2;
        }
    }
}
int SwitchPowerBomb_Window(int line,int *left,int *right) {
    if(gMainGameMode!=GM_INGAME || !gHazeInfo.active || line< -16 || line>=176 ||
       (gCurrentHazeValue!=HAZE_VALUE_POWER_BOMB_EXPANDING && gCurrentHazeValue!=HAZE_VALUE_POWER_BOMB_RETRACTING)) return 0;
    int shift=SwitchWide_CenterShift();
    *left=window_left[line+16]+shift;*right=window_right[line+16]+shift;
    return 1;
}
