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
static uint8_t occupied[4][MODE1_WS_SHADOW_ROWS*MODE1_WS_SHADOW_COLS];
static uint16_t shadow[4][MODE1_WS_SHADOW_ROWS*MODE1_WS_SHADOW_COLS];
static uint16_t backdrop[4096];
static int backdrop_area=-1,backdrop_room=-1;
static unsigned backdrop_bytes;
static int backdrop_right=288;
int SwitchWide_BackdropWidth(int width) { return width<backdrop_right ? width : backdrop_right; }
int SwitchWide_Margin(void) {
    return SwitchOverlay_Scaling()==3 && gMainGameMode==GM_INGAME && gSubGameMode1==2 && !gDisableDrawingSamusAndScrolling &&
           !(gInGameCutscene.cutsceneNumber&IGC_NO_STARTED_FLAG) &&
           gColorFading.type!=COLOR_FADING_DOOR_TRANSITION && gColorFading.type!=COLOR_FADING_WHITE ? 48 : 0;
}
void SwitchWide_Prepare(void) {
    bool active=SwitchWide_Margin()!=0;
    backdrop_right=240;
    virtuappu_mode1_ws_hud_right_anchor=0;
    virtuappu_mode1_ws_reveal_mask=0;
    virtuappu_registers.frame_width=active ? 288 : 240;
    virtuappu_registers.frame_pitch=active ? 288 : 240;
    for (int bg=0;bg<4;++bg) { virtuappu_mode1_ws_shadow[bg]=NULL; virtuappu_mode1_ws_shadow_valid[bg]=NULL; }
    if (!active) return;
    /* Liquids use a complete repeating BG0 map, not streamed room blocks. */
    if (!(gCurrentRoomEntry.bg0Prop&BG_PROP_RLE_COMPRESSED) && gCurrentRoomEntry.effectY!=UINT16_MAX)
        virtuappu_mode1_ws_reveal_mask|=1u;
    int room_width=gBgPointersAndDimensions.backgrounds[1].width*16;
    int camera_x=gBg1XPosition/4;
    int shake_x=((int)(gba_read16(0x04000014)&511)-camera_x)&511;
    if (shake_x>=256) shake_x-=512;
    int right=room_width>0 ? room_width-camera_x-shake_x : 288;
    backdrop_right=right<240 ? 240 : (right>288 ? 288 : right);
    /* BG3 is a complete repeating backdrop tilemap, rather than a streamed
     * room block map. Its normal hardware sampling is valid across Wide. */
    /* Door transitions reuse BG3 with character base 1. Only the room
     * backdrop (character base 2) can safely repeat into the reveal. */
    unsigned bg3cnt=gba_read16(0x0400000e);
    if ((bg3cnt&12u)==8u) {
        if (backdrop_area!=gCurrentArea || backdrop_room!=gCurrentRoom) {
            extern unsigned SwitchRoom_LoadBackdrop(uint8_t *destination,unsigned capacity);
            backdrop_bytes=SwitchRoom_LoadBackdrop((uint8_t*)backdrop,sizeof(backdrop));
            backdrop_area=gCurrentArea; backdrop_room=gCurrentRoom;
        }
        unsigned sx=gba_read16(0x0400001c)&511,sy=gba_read16(0x0400001e)&511;
        unsigned width=(bg3cnt&0x4000u) ? 64 : 32,height=(bg3cnt&0x8000u) ? 64 : 32;
        virtuappu_mode1_ws_shadow_base_tile[3]=((sx+240)>>3)&31;
        for (unsigned row=0;row<32;++row) for (unsigned col=0;col<MODE1_WS_SHADOW_COLS;++col) {
            unsigned x=(((sx+240)>>3)+col)&(width-1);
            unsigned y=((sy>>3)+((row-(sy>>3)+32)&31))&(height-1);
            unsigned index=((y>>5)*(width>>5)+(x>>5))*1024+(y&31)*32+(x&31);
            unsigned target=row*MODE1_WS_SHADOW_COLS+col;
            occupied[3][target]=index*2<backdrop_bytes;
            shadow[3][target]=occupied[3][target] ? backdrop[index] : 0;
        }
        virtuappu_mode1_ws_shadow[3]=shadow[3];
        virtuappu_mode1_ws_shadow_valid[3]=occupied[3];
        virtuappu_mode1_ws_reveal_mask|=8u;
    }
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
/* Center the native view between two 24-pixel reveal margins. Tilemap/IO
 * edits last only for presentation; game logic and saved state retain native coordinates. */
#include <string.h>
static uint8_t centered_map_backup[0x4000];
static uint16_t centered_scroll_backup[4];
static int center_shift,center_left,center_right;
int SwitchWide_CenterShift(void) { return center_shift; }
int SwitchWide_OutsideRoom(int x) { return center_shift && (x<center_left || x>=center_right); }
int SwitchWide_BgOutside(int bg,int x) { return SwitchWide_OutsideRoom(x) ||
    (center_shift && !(virtuappu_mode1_ws_reveal_mask&(1u<<bg)) && (x<24 || x>=264)); }
void SwitchWide_CenterBegin(void) {
    center_shift=0;
    if (!SwitchWide_Margin()) return;
    center_shift=24;
    memcpy(centered_map_backup,gVram,sizeof(centered_map_backup));
    int camera=gBg1XPosition/4;
    int shake=((int)(gba_read16(0x04000014)&511)-camera)&511;if(shake>=256)shake-=512;
    camera+=shake;
    int room_width=gBgPointersAndDimensions.backgrounds[1].width*16;
    center_left=24-camera;if(center_left<0)center_left=0;
    center_right=24+room_width-camera;if(center_right>288)center_right=288;
    if (room_width<=0) { center_left=0;center_right=288; }
    backdrop_right=center_right;
    const uint16_t *tiles=GBA_RESOLVE(gTilemapAndClipPointers.pTilemap);
    const unsigned px[3]={gBg0XPosition,gBg1XPosition,gBg2XPosition};
    const unsigned py[3]={gBg0YPosition,gBg1YPosition,gBg2YPosition};
    const unsigned props[3]={gCurrentRoomEntry.bg0Prop,gCurrentRoomEntry.bg1Prop,gCurrentRoomEntry.bg2Prop};
    for(int bg=0;bg<4;++bg) {
        unsigned cnt=gba_read16(0x04000008+bg*2);
        unsigned sx=gba_read16(0x04000010+bg*4)&511,sy=gba_read16(0x04000012+bg*4)&511;
        centered_scroll_backup[bg]=gba_read16(0x04000010+bg*4);
        unsigned map_width=(cnt&0x4000) ? 64:32,map_height=(cnt&0x8000) ? 64:32;
        unsigned screen_base=((cnt>>8)&31)*0x800;
        const uint16_t *blocks=bg<3 ? GBA_RESOLVE(gBgPointersAndDimensions.backgrounds[bg].pDecomp):NULL;
        int w=bg<3 ? gBgPointersAndDimensions.backgrounds[bg].width:0,h=bg<3 ? gBgPointersAndDimensions.backgrounds[bg].height:0;
        int rx=bg<3 ? (int)px[bg]/4:0,ry=bg<3 ? (int)py[bg]/4:0;
        int dx=((int)sx-rx)&511,dy=((int)sy-ry)&511;if(dx>=256)dx-=512;if(dy>=256)dy-=512;
        for(int yy=-(int)(sy&7);yy<160;yy+=8) for(int xx=-24-(int)(sx&7);xx<264;xx+=8) {
            if(xx>=0 && xx+8<=240)continue;
            unsigned tx=((int)sx+xx)&(map_width*8-1),ty=((int)sy+yy)&(map_height*8-1);
            unsigned index=((ty>>8)*(map_width>>5)+(tx>>8))*1024+((ty>>3)&31)*32+((tx>>3)&31);
            uint16_t entry=0;bool valid=false;
            if(bg==3 && (cnt&12)==8 && index*2<backdrop_bytes) {entry=backdrop[index];valid=true;}
            else if(bg<3 && (props[bg]&BG_PROP_RLE_COMPRESSED) && blocks && tiles) {
                int wx=(rx+dx+xx)>>3,wy=(ry+dy+yy)>>3,bx=wx>>1,by=wy>>1;
                if(bx>=0 && by>=0 && bx<w && by<h) {unsigned block=blocks[by*w+bx];if(block<0x500){entry=tiles[block*4+(wy&1)*2+(wx&1)];valid=true;}}
            }
            if(valid && screen_base+index*2+1<sizeof(centered_map_backup)) {
                gVram[screen_base+index*2]=entry;gVram[screen_base+index*2+1]=entry>>8;
            }
        }
        virtuappu_mode1_ws_shadow[bg]=NULL;virtuappu_mode1_ws_shadow_valid[bg]=NULL;
        gba_write16(0x04000010+bg*4,(sx-24)&511);
    }
}
void SwitchWide_CenterEnd(void) {
    if(!center_shift)return;
    memcpy(gVram,centered_map_backup,sizeof(centered_map_backup));
    for(int bg=0;bg<4;++bg)gba_write16(0x04000010+bg*4,centered_scroll_backup[bg]);
    center_shift=0;
}
