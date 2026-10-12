/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wide_view.h"
#include "screen_overlay.h"
#include "virtuappu.h"
#include "cpu/mode1.h"
#include "port_gba_mem.h"
#include "structs/bg_clip.h"
#include "structs/clipdata.h"
#include "structs/room.h"
#include "structs/game_state.h"
#include "structs/in_game_cutscene.h"
#include "structs/samus.h"
#include "structs/color_effects.h"
#include "constants/game_state.h"
#include "constants/room.h"
#include "port_rom.h"
#include <stdint.h>
/* Room-block sampling follows the upstream 3DS WideBgSource/WideBgEntry
 * implementation; the original gameplay viewport remains unchanged. */
extern unsigned virtuappu_mode1_ws_reveal_mask;
extern const uint8_t *virtuappu_mode1_ws_shadow_valid[4];
static uint8_t occupied[3][MODE1_WS_SHADOW_ROWS*MODE1_WS_SHADOW_COLS];
static uint16_t shadow[3][MODE1_WS_SHADOW_ROWS*MODE1_WS_SHADOW_COLS];
int SwitchWide_Margin(void) {
    return SwitchOverlay_Scaling()==3 && gMainGameMode==GM_INGAME && gSubGameMode1==2 && !gDisableDrawingSamusAndScrolling &&
           !(gInGameCutscene.cutsceneNumber&IGC_NO_STARTED_FLAG) &&
           gColorFading.type!=COLOR_FADING_DOOR_TRANSITION && gColorFading.type!=COLOR_FADING_WHITE ? 48 : 0;
}
void SwitchWide_Prepare(void) {
    bool active=SwitchWide_Margin()!=0;
    virtuappu_mode1_ws_hud_right_anchor=0;
    virtuappu_mode1_ws_reveal_mask=0;
    virtuappu_registers.frame_width=active ? 288 : 240;
    virtuappu_registers.frame_pitch=active ? 288 : 240;
    for (int bg=0;bg<4;++bg) { virtuappu_mode1_ws_shadow[bg]=NULL; virtuappu_mode1_ws_shadow_valid[bg]=NULL; }
    if (!active) return;
    /* BG3 is a complete repeating backdrop tilemap, rather than a streamed
     * room block map. Its normal hardware sampling is valid across Wide. */
    virtuappu_mode1_ws_reveal_mask=1u<<3;
    const uint16_t *tiles=GBA_RESOLVE(gTilemapAndClipPointers.pTilemap);
    if (!tiles) return;
    const unsigned px[3]={gBg0XPosition,gBg1XPosition,gBg2XPosition};
    const unsigned py[3]={gBg0YPosition,gBg1YPosition,gBg2YPosition};
    const unsigned properties[3]={gCurrentRoomEntry.bg0Prop,gCurrentRoomEntry.bg1Prop,gCurrentRoomEntry.bg2Prop};
    for (int bg=0;bg<3;++bg) {
        if (!(properties[bg]&BG_PROP_RLE_COMPRESSED)) continue;
        int w=gBgPointersAndDimensions.backgrounds[bg].width,h=gBgPointersAndDimensions.backgrounds[bg].height;
        const uint16_t *blocks=GBA_RESOLVE(gBgPointersAndDimensions.backgrounds[bg].pDecomp);
        if (!blocks || w<1 || h<1 || w>256 || h>256) continue;
        unsigned sx=gba_read16(0x04000010+bg*4)&511,sy=gba_read16(0x04000012+bg*4)&511;
        int room_x=(int)px[bg]/4,room_y=(int)py[bg]/4;
        int shake_x=((int)sx-room_x)&511,shake_y=((int)sy-room_y)&511;
        if (shake_x>=256) shake_x-=512;
        if (shake_y>=256) shake_y-=512;
        int base_x=(room_x+shake_x+240)>>3,base_y=(room_y+shake_y)>>3;
        virtuappu_mode1_ws_shadow_base_tile[bg]=((sx+240)>>3)&31;
        for (int row=0;row<32;++row) for (int col=0;col<MODE1_WS_SHADOW_COLS;++col) {
            int x=base_x+col,y=base_y+((row-(int)(sy>>3)+32)&31),bx=x>>1,by=y>>1;
            uint16_t entry=0;
            occupied[bg][row*MODE1_WS_SHADOW_COLS+col]=0;
            if (bx>=0 && by>=0 && bx<w && by<h) {
                unsigned block=blocks[by*w+bx];
                if (block<0x500) { entry=tiles[block*4+(y&1)*2+(x&1)]; occupied[bg][row*MODE1_WS_SHADOW_COLS+col]=1; }
            }
            shadow[bg][row*MODE1_WS_SHADOW_COLS+col]=entry;
        }
        virtuappu_mode1_ws_shadow[bg]=shadow[bg];
        virtuappu_mode1_ws_shadow_valid[bg]=occupied[bg];
        virtuappu_mode1_ws_reveal_mask|=1u<<bg;
    }
}
