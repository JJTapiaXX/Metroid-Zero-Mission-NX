/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "switch_ui.h"
#include "updater_switch.h"
#include "game_switch.h"
#include "audio_volume.h"
#include "audio_filters.h"
#include "save_states.h"
#include "achievements_switch.h"
#include "port_gba_mem.h"
#include "gba.h"
#include "structs/samus.h"
#include "structs/minimap.h"
#include "structs/connection.h"
#include "structs/game_state.h"
#include "menus/pause_screen.h"
#include "minimap.h"
#include "data/menus/pause_screen_map_data.h"
#include "data/menus/pause_screen_data.h"
#include "data/menus/pause_screen_sub_menus_data.h"
#include "menus/pause_screen_sub_menus.h"
#include "port_rom.h"
#include "constants/game_state.h"
#include <stdio.h>
#include <string.h>

/* ABGR theme colors: deep navy, cyan instrumentation and warm gold focus. */
#define UI_BG 0xff1a1307u
#define UI_SURFACE 0xff31260du
#define UI_ACTIVE 0xff4c3f16u
#define UI_BORDER 0xff6b5823u
#define UI_CYAN 0xffd0d966u
#define UI_GOLD 0xff6bc9f5u
#define UI_TEXT 0xffeeecdbu
#define UI_MUTED 0xffaca589u
static uint32_t pixels[320 * 240];
static uint16_t other_map[32 * 32];
static int tab, area = -1, cached_area = -1, zoom = 8;
static int pan_x, pan_y, previous_x, previous_y;
static bool follow = true, dragging;
static bool volume_dragging;
static char notice[48];
static bool audio_available, audio_page, display_page;
static unsigned state_slot,achievement_page;
static uint8_t icon_gfx[0x6000];
static unsigned icon_gfx_size;
static const uint16_t *marker_oam[2];
static const uint16_t *target_oam[4];
static uint8_t pause_map_gfx[0x3000];
static uint16_t pause_map_palette[256];
static const void *cached_map_source;
static int cached_map_language=-1;
static unsigned palette_frame;
static const char *areas[] = {"BRINSTAR", "KRAID", "NORFAIR", "RIDLEY", "TOURIAN", "CRATERIA", "CHOZODIA"};
/* Compact 5x7 raster alphabet. One byte per column, low bit at the top. */
static const uint8_t glyphs[][5] = {
    {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x42,0x61,0x51,0x49,0x46},
    {0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1e},
    {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
    {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
    {0x3e,0x41,0x49,0x49,0x7a},{0x7f,8,8,8,0x7f},{0,0x41,0x7f,0x41,0},
    {0x20,0x40,0x41,0x3f,1},{0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
    {0x7f,2,0x0c,2,0x7f},{0x7f,4,8,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
    {0x7f,9,9,9,6},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},{0x3f,0x40,0x40,0x40,0x3f},
    {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},
    {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43}
};
static void pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < 320 && y >= 0 && y < 240) pixels[y * 320 + x] = color;
}
static void rect(int x, int y, int w, int h, uint32_t color) {
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) pixel(xx, yy, color);
}
static void panel(int x,int y,int w,int h,uint32_t color) {
    rect(x,y,w,h,color);
    rect(x+2,y,w-4,1,UI_BORDER);rect(x+2,y+h-1,w-4,1,UI_BORDER);
    rect(x,y+2,1,h-4,UI_BORDER);rect(x+w-1,y+2,1,h-4,UI_BORDER);
    pixel(x+1,y+1,UI_BORDER);pixel(x+w-2,y+1,UI_BORDER);
    pixel(x+1,y+h-2,UI_BORDER);pixel(x+w-2,y+h-2,UI_BORDER);
    pixel(x,y,UI_BG);pixel(x+w-1,y,UI_BG);pixel(x,y+h-1,UI_BG);pixel(x+w-1,y+h-1,UI_BG);
}
static void text(int x, int y, const char *s, uint32_t color) {
    for (; *s; ++s, x += 6) {
        int ch=(unsigned char)*s; if (ch>='a' && ch<='z') ch-=32;
        int id = (ch >= '0' && ch <= '9') ? ch - '0' :
                 (ch >= 'A' && ch <= 'Z') ? ch - 'A' + 10 : -1;
        if (id >= 0) {
            for (int column = 0; column < 5; ++column)
                for (int row = 0; row < 7; ++row)
                    if (glyphs[id][column] & (1u << row)) pixel(x + column, y + row, color);
        } else if (*s == ':') { pixel(x+2,y+2,color); pixel(x+2,y+5,color); }
        else if (*s == '/') for (int i=0;i<5;++i) pixel(x+i,y+6-i,color);
        else if (*s == '.') pixel(x+2,y+6,color);
        else if (*s == '-') rect(x,y+3,5,1,color);
        else if (*s == '+') { rect(x,y+3,5,1,color); rect(x+2,y+1,1,5,color); }
        else if (*s == '%') {
            rect(x,y,2,2,color); rect(x+3,y+5,2,2,color);
            for (int i=0;i<5;++i) pixel(x+i,y+5-i,color);
        }
        else if (*s == '<' || *s == '>') for (int i=0;i<4;++i) {
            int xx = *s == '<' ? x+3-i : x+i;
            pixel(xx,y+i,color); pixel(xx,y+6-i,color);
        }
    }
}
static uint32_t gba_color(uint16_t color) {
    unsigned r = color & 31, g = (color >> 5) & 31, b = (color >> 10) & 31;
    return 0xff000000 | ((r << 3) | (r >> 2)) |
           (((g << 3) | (g >> 2)) << 8) | (((b << 3) | (b >> 2)) << 16);
}
static void map_tile(int x, int y, uint16_t tile) {
    unsigned id = tile & 0x3ff, palette = tile >> 12;
    if (tile == 0 || tile == 0x140) return;
    /* Match PauseScreenInit's full 0x3000-byte upload and tile indices.
     * The original 5120-byte declaration only describes the first block. */
    if (id >= sizeof(pause_map_gfx)/32 || !cached_map_source) return;
    const uint8_t *gfx = pause_map_gfx + id * 32;
    const uint16_t *pal = pause_map_palette + palette * 16;
    for (int yy = 0; yy < zoom; ++yy) {
        int ty = yy * 8 / zoom;
        if (tile & 0x800) ty = 7 - ty;
        for (int xx = 0; xx < zoom; ++xx) {
            int tx = xx * 8 / zoom;
            if (tile & 0x400) tx = 7 - tx;
            unsigned byte = gfx[ty * 4 + tx / 2];
            unsigned index = (tx & 1) ? byte >> 4 : byte & 15;
            if (index && y+yy >= 32 && y+yy < 210) pixel(x+xx,y+yy,gba_color(pal[index]));
        }
    }
}
/* Decode assets into private popup storage; gameplay VRAM stays untouched. */
static bool decompress_icon(const uint8_t *src) {
    uintptr_t offset=(uintptr_t)src-(uintptr_t)gRomData;
    if (!gRomData || offset>gRomSize || gRomSize-offset<4 || src[0]!=0x10) return false;
    unsigned size=(unsigned)src[1]|(unsigned)src[2]<<8|(unsigned)src[3]<<16;
    if (!size || size>sizeof(icon_gfx)) return false;
    const uint8_t *end=gRomData+gRomSize; src+=4;
    unsigned written=0;
    while (written<size) {
        if (src>=end) return false;
        unsigned flags=*src++;
        for (int bit=7;bit>=0 && written<size;--bit) {
            if (flags&(1u<<bit)) {
                if (end-src<2) return false;
                unsigned length=(src[0]>>4)+3,distance=(((unsigned)src[0]&15)<<8|src[1])+1; src+=2;
                if (distance>written) return false;
                while (length-- && written<size) { icon_gfx[written]=icon_gfx[written-distance]; ++written; }
            } else {
                if (src>=end) return false;
                icon_gfx[written++]=*src++;
            }
        }
    }
    icon_gfx_size=size; return true;
}
static void samus_marker(int center_x,int center_y) {
    if (!icon_gfx_size || !p_sPauseScreenSamusIconOam || !p_sTankIconsPal) return;
    unsigned kind=gEquipment.suitType==SUIT_SUITLESS ? SAMUS_ICON_OAM_ID_SUITLESS : SAMUS_ICON_OAM_ID_SUIT;
    const uint16_t *oam=marker_oam[kind==SAMUS_ICON_OAM_ID_SUITLESS];
    if (!oam || oam[0]!=1) return;
    unsigned a0=oam[1],a1=oam[2],a2=oam[3];
    int dx=a1&511,dy=a0&255;
    if (dx>=256) dx-=512;
    if (dy>=128) dy-=256;
    /* Both original first-frame markers are 16x8, 4bpp, horizontal objects. */
    if ((a0>>14)!=1 || (a1>>14)!=0 || (a0&0x2000)) return;
    unsigned first=a2&1023;
    if (first<0x180) return;
    const uint16_t *pal=sTankIconsPal+((a2>>12)&15)*16;
    for (int yy=0;yy<8;++yy) for (int xx=0;xx<16;++xx) {
        unsigned tx=(a1&0x1000) ? 15-xx : xx,ty=(a1&0x2000) ? 7-yy : yy;
        unsigned offset=(first-0x180+tx/8)*32+ty*4+(tx%8)/2;
        if (offset>=icon_gfx_size) continue;
        unsigned byte=icon_gfx[offset],index=(tx&1) ? byte>>4 : byte&15;
        int x=center_x+dx+xx,y=center_y+dy+yy;
        if (index && y>=32 && y<210) pixel(x,y,gba_color(pal[index]));
    }
}
static void prepare_pause_map_assets(void) {
    if (!p_sMinimapTilesGfx || !p_sMinimapTilesPal || !p_sPauseScreen_3fcef0) return;
    const uint8_t *source=*p_sMinimapTilesGfx;
    uintptr_t offset=(uintptr_t)source-(uintptr_t)gRomData;
    if (!gRomData || offset>gRomSize || sizeof(pause_map_gfx)>gRomSize-offset) return;
    if (cached_map_source!=source || cached_map_language!=gLanguage) {
        memcpy(pause_map_gfx,source,sizeof(pause_map_gfx));
        memcpy(pause_map_palette,*p_sMinimapTilesPal,5*16*sizeof(uint16_t));
        memcpy(pause_map_palette+5*16,*p_sPauseScreen_3fcef0,11*16*sizeof(uint16_t));
        pause_map_palette[0]=0;
        if (gRomRegion==PORT_ROM_REGION_JP && gLanguage==LANGUAGE_HIRAGANA)
            memcpy(pause_map_gfx+0x2820,pause_map_gfx+0x2c20,0x3e0);
        else if (gRomRegion!=PORT_ROM_REGION_JP)
            memcpy(pause_map_gfx+0x2c20,pause_map_gfx+0x2820,0x3e0);
        if (p_sTankIconsGfx) decompress_icon((const uint8_t*)*p_sTankIconsGfx);
        cached_map_source=source; cached_map_language=gLanguage;
    }
    /* Retail animated colors for save rooms and map markers. */
    unsigned frame=(palette_frame++/8)%4;
    if (p_sPauseScreen_40dcb0 && p_sPauseScreen_40dc90) {
        pause_map_palette[18]=pause_map_palette[19]=(*p_sPauseScreen_40dcb0)[frame];
        pause_map_palette[50]=(*p_sPauseScreen_40dc90)[frame];
    }
}
void SwitchUI_PrepareAssets(void) {
    prepare_pause_map_assets();
    if (!p_sPauseScreenSamusIconOam) return;
    for (unsigned i=0;i<2;++i) {
        unsigned kind=i ? SAMUS_ICON_OAM_ID_SUITLESS : SAMUS_ICON_OAM_ID_SUIT;
        const struct FrameData *frames=GBA_RESOLVE(sPauseScreenSamusIconOam[kind].pOam);
        marker_oam[i]=frames ? GBA_RESOLVE(frames[0].pFrame) : NULL;
    }
    if(p_sPauseScreenTargetsOam) {
        const struct FrameData *frames=GBA_RESOLVE(sPauseScreenTargetsOam[TARGET_OAM_ID_TARGET].pOam);
        for(unsigned i=0;i<4;++i)target_oam[i]=frames ? GBA_RESOLVE(frames[i].pFrame):NULL;
    }
}
static void objective_marker(int center_x,int center_y) {
    const uint16_t *oam=target_oam[(palette_frame/8)%4];
    if(!oam || oam[0]!=1 || !icon_gfx_size)return;
    unsigned tile=oam[3]&1023,bank=(oam[3]>>12)&15;
    if(tile<0x180 || (tile-0x180)*32+32>icon_gfx_size)return;
    const uint8_t *gfx=icon_gfx+(tile-0x180)*32;
    const uint16_t *pal=sTankIconsPal+bank*16;
    for(int y=0;y<zoom;++y)for(int x=0;x<zoom;++x) {
        unsigned tx=x*8/zoom,ty=y*8/zoom;
        unsigned byte=gfx[ty*4+tx/2],index=(tx&1) ? byte>>4:byte&15;
        int px=center_x-zoom/2+x,py=center_y-zoom/2+y;
        if(index && py>=32 && py<210)pixel(px,py,gba_color(pal[index]));
    }
}
void SwitchUI_Notice(const char *message) { snprintf(notice,sizeof(notice),"%s",message); }
void SwitchUI_SetAudioAvailable(bool available) { audio_available=available; }

const uint32_t *SwitchUI_Render(const SwitchInput *in) {
    if (in->overlay_visible && in->tab_delta) tab=(tab+in->tab_delta+5)%5;
    if (area < 0 || (follow && !dragging)) area = gCurrentArea < 7 ? gCurrentArea : 0;
    /* Each new press owns its gesture. A missed release must not let an old
     * slider drag overwrite a minus/plus click with the endpoint value. */
    if (volume_dragging && (!in->pointer_held || !in->overlay_visible || tab!=4 || in->pointer_pressed)) {
        AudioVolume_Save(); volume_dragging=false;
    }
    if (in->overlay_visible && in->pointer_pressed) {
        if (in->pointer_y < 24) { tab = in->pointer_x / 64; if (tab > 4) tab = 4; }
        else if (tab == 0 && in->pointer_y >= 214) {
            if (in->pointer_x<32) { area=(area+6)%7; follow=false; }
            else if (in->pointer_x<80) { zoom=zoom>4 ? zoom-4 : 4; }
            else if (in->pointer_x>=288) { area=(area+1)%7; follow=false; }
            else if (in->pointer_x>=240) { zoom=zoom<16 ? zoom+4 : 16; }
            else follow=!follow;
        } else if (tab == 0 && in->pointer_y >= 32 && in->pointer_y < 210) {
            dragging = true; follow = false;
            previous_x = in->pointer_x; previous_y = in->pointer_y;
        } else if (tab==2) {
            if (in->pointer_y>=44 && in->pointer_y<80) state_slot=(unsigned)in->pointer_x*5/320;
            if (in->pointer_y>=96 && in->pointer_y<132) SaveStates_Save(state_slot);
            if (in->pointer_y>=144 && in->pointer_y<180) SaveStates_Load(state_slot);
        } else if (tab==3) {
            if (in->pointer_y>=52 && in->pointer_y<74 && in->pointer_x<106) Achievements_Login();
            else if (in->pointer_y>=52 && in->pointer_y<74) {
                if (in->pointer_x<214) Achievements_Reconnect(); else Achievements_Disable();
            } else if (in->pointer_y>=216) {
                if (in->pointer_x<160 && achievement_page) --achievement_page;
                else if (in->pointer_x>=160 && (achievement_page+1)*4+1<Achievements_Count()) ++achievement_page;
            }
        } else if (tab==4 && in->pointer_y>=26 && in->pointer_y<44) {
            audio_page=in->pointer_x>=107 && in->pointer_x<214;
            display_page=in->pointer_x>=214;
        } else if (tab == 4) {
            bool page_before=audio_page;
            if (display_page) {
                if (in->pointer_y>=216) display_page=false;
                else if (in->pointer_y>=64 && in->pointer_y<96)
                    SwitchOverlay_SetOptions(in->pointer_x<110 ? 0 : in->pointer_x<210 ? 1 : SwitchOverlay_Layout()==2 ? 5:2,SwitchOverlay_Scaling());
                else if (in->pointer_y>=98 && in->pointer_y<128 && in->pointer_x>=12 && in->pointer_x<308)
                    SwitchOverlay_SetOptions(in->pointer_x<160 ? 3 : 4,SwitchOverlay_Scaling());
                else if (in->pointer_y>=136 && in->pointer_y<168)
                    SwitchOverlay_SetOptions(SwitchOverlay_Layout(),(SwitchOverlay_Scaling()+1)%4);
                else if (in->pointer_y>=176 && in->pointer_y<208) {
                    if(in->pointer_x<160) SwitchOverlay_ShowFps(!SwitchOverlay_FpsVisible());
                    else SwitchOverlay_SetLcd((SwitchOverlay_Lcd()+1)%4);
                }
                SwitchOverlay_SaveOptions();
            } else if (audio_page) {
                int y=in->pointer_y;
                if (y>=216) AudioFilters_Defaults();
                else if (y>=88 && y<108 && in->pointer_x>=164) AudioFilters_Set(AUDIO_SFX,(AudioFilters_Get(AUDIO_SFX)+1)%5);
                else if (y>=88 && y<108) AudioFilters_Set(AUDIO_TRIM,(AudioFilters_Get(AUDIO_TRIM)+1)%3);
                else if (y>=112 && y<132) AudioFilters_Set(AUDIO_FILTER,(AudioFilters_Get(AUDIO_FILTER)+1)%3);
                else if (y>=136 && y<156) AudioFilters_Set(AUDIO_DC,!AudioFilters_Get(AUDIO_DC));
                else if (y>=160 && y<180) AudioFilters_Set(AUDIO_LIMITER,!AudioFilters_Get(AUDIO_LIMITER));
                else if (y>=184 && y<208) {
                    enum AudioOption option=in->pointer_x<160 ? AUDIO_PSG : AUDIO_REVERB;
                    AudioFilters_Set(option,!AudioFilters_Get(option));
                }
                if (y>=88) AudioFilters_Save();
            } else {
                if (in->pointer_y >= 48 && in->pointer_y < 80) zoom=zoom==8 ? 4 : 8;
                if (in->pointer_y >= 88 && in->pointer_y < 120) follow=!follow;
                if (in->pointer_y >= 128 && in->pointer_y < 160) Updater_Action();
            }
            int volume_y=audio_page ? 60 : 142;
            if (!display_page && audio_page && page_before==audio_page && in->pointer_y >= volume_y && in->pointer_y < volume_y+28) {
                if (in->pointer_x < 42) AudioVolume_Set(AudioVolume_Get() >= 1 ? AudioVolume_Get()-1 : 0);
                else if (in->pointer_x >= 278) AudioVolume_Set(AudioVolume_Get()+1);
                else if (in->pointer_x >= 48 && in->pointer_x <= 272) volume_dragging=true;
                if (!volume_dragging) AudioVolume_Save();
            }
        }
    }
    if (volume_dragging && !display_page && in->pointer_held && in->overlay_visible && tab==4) {
        int x=in->pointer_x;
        if (x<48) x=48;
        if (x>272) x=272;
        AudioVolume_Set((unsigned)(x-48)*150/224);
    }
    if (volume_dragging && (in->touch_released || !in->overlay_visible || tab!=4)) {
        AudioVolume_Save(); volume_dragging=false;
    }
    if (dragging && in->pointer_held) {
        pan_x -= in->pointer_x - previous_x;
        pan_y -= in->pointer_y - previous_y;
        previous_x = in->pointer_x; previous_y = in->pointer_y;
    }
    if (in->touch_released || !in->overlay_visible) dragging = false;
    for (size_t i=0;i<320*240;++i) pixels[i]=UI_BG;
    const char *tabs[]={"MAP","STATUS","STATES","ACHIEVE","OPTIONS"};
    for (int i=0;i<5;++i) {
        panel(i*64,0,63,24,i==tab ? UI_ACTIVE : UI_SURFACE);
        text(i*64+(64-(int)strlen(tabs[i])*6+1)/2,8,tabs[i],i==tab ? UI_GOLD : UI_MUTED);
        if(i==tab)rect(i*64+4,21,56,2,UI_GOLD);
    }
    if (tab == 0) {
        prepare_pause_map_assets();
        text(8,25,areas[area],UI_CYAN); text(266,25,"TARGET",UI_GOLD);
        const uint16_t *map = gDecompressedMinimapVisitedTiles;
        if (area != gCurrentArea) {
            if (cached_area != area) {
                PauseScreenGetMinimapData(area,other_map);
                MinimapSetDownloadedTiles(area,other_map); cached_area=area;
            }
            map=other_map;
        }
        if (follow) { pan_x = gMinimapX*zoom-160; pan_y=gMinimapY*zoom-88; }
        if (pan_x < -160) pan_x=-160;
        if (pan_y < -88) pan_y=-88;
        if (pan_x > 32*zoom-160) pan_x=32*zoom-160;
        if (pan_y > 32*zoom-88) pan_y=32*zoom-88;
        /* The retail map background repeats tile 0x140 in palette bank 1. */
        int grid_x=-((pan_x%zoom+zoom)%zoom),grid_y=32-((pan_y%zoom+zoom)%zoom);
        for (int y=grid_y;y<210;y+=zoom) for (int x=grid_x;x<320;x+=zoom) map_tile(x,y,0x1140);
        for (int y=0;y<32;++y) for (int x=0;x<32;++x)
            map_tile(x*zoom-pan_x,32+y*zoom-pan_y,map[y*32+x]);
        for(unsigned i=0;i<TARGET_COUNT;++i) {
            if(ChozoStatueHintCheckTargetIsActivated(i)<=0)continue;
            const struct ChozoStatueTarget *target=&sChozoStatueTargets[i];
            if(target->targetArea==area && target->endIcon==TARGET_OAM_ID_TARGET)
                objective_marker(target->targetX*zoom-pan_x,32+target->targetY*zoom-pan_y);
        }
        if (area == gCurrentArea) {
            int x=gMinimapX*zoom-pan_x, y=32+gMinimapY*zoom-pan_y;
            samus_marker(x+zoom/2,y+zoom/2);
        }
        rect(0,212,320,28,UI_BG);rect(0,212,320,1,UI_BORDER);
        panel(4,216,24,22,UI_SURFACE);panel(32,216,48,22,UI_SURFACE);
        panel(84,216,152,22,follow ? UI_ACTIVE:UI_SURFACE);
        panel(240,216,48,22,UI_SURFACE);panel(292,216,24,22,UI_SURFACE);
        text(13,224,"<",UI_TEXT); text(39,224,"- ZOOM",UI_TEXT); text(247,224,"+ ZOOM",UI_TEXT); text(301,224,">",UI_TEXT);
        const char *label=follow ? "FOLLOW ON" : "FOLLOW OFF";
        text((320-(int)strlen(label)*6+1)/2,224,label,follow ? UI_GOLD:UI_MUTED);
    } else if (tab == 1) {
        const char *names[]={"ENERGY","MISSILES","SUPER MISSILES","POWER BOMBS"};
        unsigned current[]={gEquipment.currentEnergy,gEquipment.currentMissiles,gEquipment.currentSuperMissiles,gEquipment.currentPowerBombs};
        unsigned maximum[]={gEquipment.maxEnergy,gEquipment.maxMissiles,gEquipment.maxSuperMissiles,gEquipment.maxPowerBombs};
        for(unsigned i=0;i<4;++i) {
            int x=12+(i%2)*152,y=34+(i/2)*40;char value[24];
            panel(x,y,144,32,UI_SURFACE);text(x+10,y+6,names[i],UI_MUTED);
            snprintf(value,sizeof(value),"%u / %u",current[i],maximum[i]);
            text(x+134-(int)strlen(value)*6,y+17,value,i==0 ? UI_GOLD:UI_TEXT);
            rect(x+10,y+27,124,2,UI_BORDER);
            unsigned fill=maximum[i] ? current[i]*124/maximum[i]:0;if(fill>124)fill=124;
            rect(x+10,y+27,fill,2,i==0 ? UI_GOLD:UI_CYAN);
        }
        text(12,116,"BEAM SYSTEMS",UI_CYAN);text(164,116,"SUIT SYSTEMS",UI_CYAN);
        panel(12,128,144,84,UI_SURFACE);panel(164,128,144,84,UI_SURFACE);
        const char *beams[]={"LONG BEAM","ICE BEAM","WAVE BEAM","PLASMA BEAM","CHARGE BEAM","BOMBS"};
        const unsigned beam_bits[]={1,2,4,8,16,128};
        const char *abilities[]={"HIGH JUMP","SPEED BOOST","SPACE JUMP","SCREW ATTACK","VARIA SUIT","GRAVITY SUIT","MORPH BALL","POWER GRIP"};
        for (unsigned i=0;i<6;++i) {
            bool owned=(gEquipment.beamBombs&beam_bits[i])!=0;
            rect(20,134+i*13,3,3,owned ? UI_CYAN:UI_BORDER);
            text(28,132+i*13,beams[i],owned ? UI_TEXT:UI_MUTED);
        }
        for (unsigned i=0;i<8;++i) {
            bool owned=(gEquipment.suitMisc&(1u<<i))!=0;
            rect(172,134+i*10,3,3,owned ? UI_CYAN:UI_BORDER);
            text(180,132+i*10,abilities[i],owned ? UI_TEXT:UI_MUTED);
        }
        rect(0,214,320,26,UI_BG);rect(0,214,320,1,UI_BORDER);text(18,224,SwitchOverlay_Layout() ? "LIVE STATUS":"R3 CLOSES",UI_MUTED);
    } else if (tab==2) {
        text(18,30,"SAVE STATES - SOFTCORE",UI_CYAN);
        for (unsigned i=0;i<5;++i) {
            char slot[12]; snprintf(slot,sizeof(slot),"%u %s",i+1,SaveStates_Exists(i) ? "USED" : "EMPTY");
            panel(i*64+2,44,60,36,i==state_slot ? UI_ACTIVE : UI_SURFACE);
            text(i*64+2+(60-(int)strlen(slot)*6+1)/2,59,slot,i==state_slot ? UI_GOLD:UI_MUTED);
        }
        panel(12,96,296,36,UI_SURFACE); text((320-18*6+1)/2,110,"SAVE SELECTED SLOT",UI_TEXT);
        panel(12,144,296,36,UI_SURFACE); text((320-18*6+1)/2,158,"LOAD SELECTED SLOT",UI_TEXT);
        text(12,194,SaveStates_Message(),UI_CYAN);
        panel(8,216,304,20,UI_SURFACE);text(18,222,"STATES REQUIRE MATCHING BUILD",UI_MUTED);
    } else if (tab==3) {
        char row[64];
        unsigned visible_count=Achievements_Count() ? Achievements_Count()-1 : 0,earned_count=0;
        for (unsigned i=1;i<Achievements_Count();++i) if (Achievements_Unlocked(i)) ++earned_count;
        snprintf(row,sizeof(row),"%u OF %u UNLOCKED",earned_count,visible_count); text(12,30,row,UI_TEXT);
        text(12,42,Achievements_ConnectionLabel(),UI_CYAN);
        panel(12,52,94,22,UI_SURFACE); text(30,60,"SIGN IN",UI_TEXT);
        panel(112,52,96,22,UI_SURFACE); text(130,60,"RECONNECT",UI_TEXT);
        panel(214,52,94,22,UI_SURFACE); text(238,60,"TURN OFF",UI_TEXT);
        unsigned count=visible_count,pages=(count+3)/4;
        if (pages && achievement_page>=pages) achievement_page=pages-1;
        if (!count) text(12,104,"SIGN IN TO LOAD THE ACHIEVEMENT LIST",UI_TEXT);
        for (unsigned i=0;i<4;++i) {
            unsigned index=achievement_page*4+i+1; if (index>=Achievements_Count()) break;
            int y=80+(int)i*32; bool earned=Achievements_Unlocked(index);
            panel(8,y,304,30,earned ? UI_ACTIVE : UI_SURFACE);
            const uint32_t *badge=Achievements_Badge(index);
            if (badge) for (int yy=0;yy<20;++yy) for (int xx=0;xx<20;++xx) pixel(12+xx,y+5+yy,badge[yy*20+xx]);
            snprintf(row,sizeof(row),"%.35s",Achievements_Title(index)); text(40,y+2,row,UI_TEXT);
            snprintf(row,sizeof(row),"%s  %u POINTS",earned ? "UNLOCKED" : Achievements_Supported(index) ? "LOCKED" : "UNAVAILABLE",Achievements_Points(index)); text(40,y+12,row,earned ? UI_CYAN : UI_MUTED);
            snprintf(row,sizeof(row),"%.43s",Achievements_Description(index)); text(40,y+22,row,UI_MUTED);
        }
        panel(8,216,112,20,UI_SURFACE);panel(200,216,112,20,UI_SURFACE);text(12,224,"< PREVIOUS",UI_TEXT); text(254,224,"NEXT >",UI_TEXT);
        snprintf(row,sizeof(row),"%u / %u",pages ? achievement_page+1 : 0,pages); text(140,224,row,UI_TEXT);
    } else {
        for (int i=0;i<3;++i) {
            bool selected=i==0 ? !audio_page&&!display_page : i==1 ? audio_page : display_page;
            panel(i*107,26,106,18,selected ? UI_ACTIVE : UI_SURFACE);
            if(selected)rect(i*107+5,41,96,1,UI_CYAN);
        }
        text(30,32,"GENERAL",!audio_page&&!display_page ? UI_GOLD:UI_MUTED); text(143,32,"AUDIO",audio_page ? UI_GOLD:UI_MUTED); text(243,32,"DISPLAY",display_page ? UI_GOLD:UI_MUTED);
        if (display_page) {
            text(22,50,"DISPLAY SETTINGS",UI_TEXT);
            const char *layouts[]={"R3 POPUP","SIDE BY SIDE",SwitchOverlay_Layout()==5 ? "FLIP 270":"FLIP GRIP"};
            for (unsigned i=0;i<3;++i) {
                panel(12+(int)i*100,64,96,32,(SwitchOverlay_Layout()==i || (i==2 && SwitchOverlay_Layout()==5)) ? UI_ACTIVE : UI_SURFACE);
                text(12+(int)i*100+(96-(int)strlen(layouts[i])*6)/2,76,layouts[i],(SwitchOverlay_Layout()==i || (i==2 && SwitchOverlay_Layout()==5)) ? UI_GOLD:UI_TEXT);
            }
            const char *corners[]={"SCREEN ON TOP","CORNER BESIDE"};
            for (unsigned i=0;i<2;++i) {
                int left=12+(int)i*150;
                panel(left,98,146,30,SwitchOverlay_Layout()==i+3 ? UI_ACTIVE:UI_SURFACE);
                text(left+(146-(int)strlen(corners[i])*6)/2,109,corners[i],SwitchOverlay_Layout()==i+3 ? UI_GOLD:UI_TEXT);
            }
            panel(12,136,296,32,UI_SURFACE);
            const char *scales[]={"SCALE PIXEL PERFECT","SCALE FIT","SCALE STRETCH","SCALE WIDE"};
            text(22,148,scales[SwitchOverlay_Scaling()],UI_TEXT);
            panel(12,176,144,32,UI_SURFACE); panel(164,176,144,32,UI_SURFACE);
            text(22,188,SwitchOverlay_FpsVisible() ? "SHOW FPS ON" : "SHOW FPS OFF",UI_TEXT);
            const char *lcd[]={"FILTER OFF","LCD SOFT","LCD GRID","SCANLINES"};text(174,188,lcd[SwitchOverlay_Lcd()],UI_TEXT);
            panel(12,216,200,20,UI_SURFACE); text(22,222,"< BACK TO OPTIONS",UI_TEXT);
            text(278,224,"1.2",UI_MUTED);
            return pixels;
        }
        if (!audio_page) {
            panel(12,48,296,32,UI_SURFACE); text(22,60,zoom==8 ? "MAP ZOOM DETAIL" : "MAP ZOOM OVERVIEW",UI_TEXT);
            panel(12,88,296,32,UI_SURFACE); text(22,100,follow ? "FOLLOW SAMUS ON" : "FOLLOW SAMUS OFF",UI_TEXT);
            panel(12,128,296,32,UI_SURFACE); text(22,140,Updater_Button(),UI_TEXT);
            text(12,170,Updater_Status(),UI_MUTED);
            text(12,184,"OLDER STATES NEED THEIR OLD BUILD",UI_MUTED);
        }
        if (audio_page) {
        char volume_label[40];
        snprintf(volume_label,sizeof(volume_label),"MASTER VOLUME %u%%",AudioVolume_Get());
        int volume_top=audio_page ? 46 : 128;
        int slider_y=audio_page ? 70 : 152;
        text(22,volume_top,volume_label,audio_available ? UI_TEXT : UI_MUTED);
        panel(12,slider_y-10,30,26,UI_SURFACE); text(24,slider_y-1,"-",UI_TEXT);
        panel(278,slider_y-10,30,26,UI_SURFACE); text(290,slider_y-1,"+",UI_TEXT);
        rect(48,slider_y,224,4,UI_MUTED);
        rect(48,slider_y,AudioVolume_Get()*224/150,4,UI_CYAN);
        rect(46+AudioVolume_Get()*224/150,slider_y-6,5,16,UI_TEXT);
        }
        if (audio_page) {
            char row[48];
            unsigned trims[]={25,50,100}; const char *filters[]={"OFF","GENTLE","STRONG"};
            for (int y=112;y<=160;y+=24) panel(12,y,296,20,UI_SURFACE);
            panel(12,88,144,20,UI_SURFACE);panel(164,88,144,20,UI_SURFACE);
            snprintf(row,sizeof(row),"TRIM %u%%",trims[AudioFilters_Get(AUDIO_TRIM)]); text(22,94,row,UI_TEXT);
            snprintf(row,sizeof(row),"SFX BOOST %u%%",100+25*AudioFilters_Get(AUDIO_SFX));text(174,94,row,UI_TEXT);
            snprintf(row,sizeof(row),"LOW PASS %s",filters[AudioFilters_Get(AUDIO_FILTER)]); text(22,118,row,UI_TEXT);
            text(22,142,AudioFilters_Get(AUDIO_DC) ? "DC REMOVAL ON" : "DC REMOVAL OFF",UI_TEXT);
            text(22,166,AudioFilters_Get(AUDIO_LIMITER) ? "PEAK LIMITER ON" : "PEAK LIMITER OFF",UI_TEXT);
            panel(12,184,144,24,UI_SURFACE); panel(164,184,144,24,UI_SURFACE);
            text(22,192,AudioFilters_Get(AUDIO_PSG) ? "PSG ON" : "PSG OFF",UI_TEXT);
            text(174,192,AudioFilters_Get(AUDIO_REVERB) ? "REVERB ON" : "REVERB OFF",UI_TEXT);
            panel(12,216,200,20,UI_SURFACE); text(22,222,"RESET AUDIO DEFAULTS",UI_TEXT);
        } else {
            text(18,224,SwitchOverlay_Layout() ? "TOUCH CONTROLS":"R3 CLOSES",UI_MUTED);
        }
        text(278,224,"1.2",UI_MUTED);
    }
    if (notice[0]) { panel(4,194,312,18,UI_ACTIVE); text(12,200,notice,UI_GOLD); }
    return pixels;
}
