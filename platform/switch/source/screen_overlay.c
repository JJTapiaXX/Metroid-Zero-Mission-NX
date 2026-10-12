/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "screen_overlay.h"
#include <string.h>



static unsigned screen_layout,screen_scaling,display_fps;
static bool show_fps;
static char toast_title[80],toast_detail[80];
static unsigned toast_frames;
static const uint32_t *toast_badge;
void SwitchOverlay_ToastBadge(const uint32_t *badge) { toast_badge=badge; if (badge && toast_frames) toast_frames=300; }
void SwitchOverlay_Toast(const char *title,const char *detail) {
    unsigned i=0; while (title[i] && i<sizeof(toast_title)-1) { toast_title[i]=title[i]; ++i; } toast_title[i]=0;
    i=0; while (detail[i] && i<sizeof(toast_detail)-1) { toast_detail[i]=detail[i]; ++i; } toast_detail[i]=0;
    toast_frames=300; toast_badge=NULL;
}
bool SwitchOverlay_FpsVisible(void) { return show_fps; }
void SwitchOverlay_ShowFps(bool visible) { show_fps=visible; }
void SwitchOverlay_SetFps(unsigned fps) { display_fps=fps>999 ? 999 : fps; }
unsigned SwitchOverlay_Layout(void) { return screen_layout; }
unsigned SwitchOverlay_Scaling(void) { return screen_scaling; }
void SwitchOverlay_SetOptions(unsigned layout,unsigned scaling) {
    screen_layout=layout>2 ? 2 : layout; screen_scaling=scaling>2 ? 2 : scaling;
}
void SwitchOverlay_SecondBounds(int *x,int *y,int *width,int *height) {
    if (screen_layout==2) { *x=16; *y=40; *width=480; *height=640; return; }
    *x=screen_layout ? 816 : 192; *y=screen_layout ? 192 : 24;
    *width=screen_layout ? 448 : 896; *height=screen_layout ? 336 : 672;
}
enum { POPUP_X = 320, POPUP_Y = 120, POPUP_W = 640, POPUP_H = 480 };

void SwitchOverlay_Update(SwitchOverlay *overlay, bool r3_held) {
    if (r3_held && !overlay->previous_r3) overlay->visible = !overlay->visible;
    overlay->previous_r3 = r3_held;
}

bool SwitchOverlay_MapPoint(const SwitchOverlay *overlay, int x, int y,
                            int *ui_x, int *ui_y) {
    int left,top,width,height;
    SwitchOverlay_SecondBounds(&left,&top,&width,&height);
    if ((!overlay->visible && !screen_layout) || !ui_x || !ui_y || x<left || y<top ||
        x>=left+width || y>=top+height) return false;
    if (screen_layout==2) {
        *ui_x=(y-top)*SWITCH_UI_WIDTH/height;
        *ui_y=SWITCH_UI_HEIGHT-1-(x-left)*SWITCH_UI_HEIGHT/width;
        return true;
    }
    *ui_x=(x-left)*SWITCH_UI_WIDTH/width;
    *ui_y=(y-top)*SWITCH_UI_HEIGHT/height;
    return true;
}

static bool valid(const SwitchSurface *s) {
    return s && s->pixels && s->width > 0 && s->height > 0 &&
        s->pitch >= (size_t)s->width;
}

static void scale(uint32_t *output, size_t pitch, const SwitchSurface *s,
                  int left, int top, int width, int height) {
    /* Expand a source row once, then copy it for repeated destination rows.
     * Cached horizontal coordinates remove division from the pixel loop. */
    static int lookup[2][SWITCH_SCREEN_WIDTH],cached_width[2],cached_source[2];
    unsigned slot=s->width==SWITCH_UI_WIDTH ? 1 : 0;
    if (cached_width[slot]!=width || cached_source[slot]!=s->width) {
        for (int x=0;x<width;++x) lookup[slot][x]=x*s->width/width;
        cached_width[slot]=width; cached_source[slot]=s->width;
    }
    int previous_y=-1;
    uint32_t *previous_row=NULL;
    for (int y=0;y<height;++y) {
        int source_y=y*s->height/height;
        uint32_t *dst=output+(size_t)(top+y)*pitch+left;
        if (source_y==previous_y) memcpy(dst,previous_row,(size_t)width*sizeof(*dst));
        else {
            const uint32_t *row=s->pixels+(size_t)source_y*s->pitch;
            for (int x=0;x<width;++x) dst[x]=row[lookup[slot][x]];
        }
        previous_y=source_y; previous_row=dst;
    }
}

/* Clockwise rotation with integer coordinate maps, avoiding per-pixel division. */
static void scale_clockwise(uint32_t *output,size_t pitch,const SwitchSurface *s,
                            int left,int top,int width,int height) {
    int source_rows[SWITCH_SCREEN_WIDTH];
    for (int x=0;x<width;++x) source_rows[x]=s->height-1-x*s->height/width;
    for (int y=0;y<height;++y) {
        int source_x=y*s->width/height;
        uint32_t *dst=output+(size_t)(top+y)*pitch+left;
        for (int x=0;x<width;++x) dst[x]=s->pixels[(size_t)source_rows[x]*s->pitch+source_x];
    }
}

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
static void toast_text(uint32_t *output,size_t pitch,int x,int y,const char *text) {
    for (unsigned character=0;*text && character<46;++text,++character,x+=12) {
        int ch=(unsigned char)*text; if (ch>='a' && ch<='z') ch-=32;
        int id=ch>='0' && ch<='9' ? ch-'0' : ch>='A' && ch<='Z' ? ch-'A'+10 : -1;
        if (id>=0) for (int column=0;column<5;++column) for (int row=0;row<7;++row)
            if (glyphs[id][column]&(1u<<row)) for (int dy=0;dy<2;++dy) for (int dx=0;dx<2;++dx)
                output[(size_t)(y+row*2+dy)*pitch+x+column*2+dx]=0xffffffff;
    }
}
static void draw_toast(uint32_t *output,size_t pitch) {
    if (!toast_frames) return;
    --toast_frames;
    for (int y=32;y<148;++y) for (int x=24;x<736;++x)
        output[(size_t)y*pitch+x]=y<35 || y>=145 || x<27 || x>=733 ? 0xff80e080 : 0xff302010;
    if (toast_badge) for (int y=0;y<80;++y) for (int x=0;x<80;++x)
        output[(size_t)(50+y)*pitch+40+x]=toast_badge[(y/4)*20+x/4];
    toast_text(output,pitch,140,48,"ACHIEVEMENT UNLOCKED");
    toast_text(output,pitch,140,80,toast_title);
    toast_text(output,pitch,140,112,toast_detail);
}
static void fps_counter(uint32_t *output,size_t pitch) {
    if (!show_fps) return;
    static const uint8_t glyph[13][5]={
        {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x42,0x61,0x51,0x49,0x46},
        {0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
        {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1e},
        {0x7f,9,9,9,1},{0x7f,9,9,9,6},{0x46,0x49,0x49,0x49,0x31}
    };
    int chars[]={10,11,12,-1,(int)(display_fps/100),(int)(display_fps/10%10),(int)(display_fps%10)};
    if (display_fps<100) chars[4]=-1;
    for (int y=8;y<30;++y) for (int x=8;x<100;++x) output[(size_t)y*pitch+x]=0xff000000;
    for (int i=0;i<7;++i) if (chars[i]>=0)
        for (int x=0;x<5;++x) for (int y=0;y<7;++y) if (glyph[chars[i]][x]&(1u<<y))
            for (int dy=0;dy<2;++dy) for (int dx=0;dx<2;++dx)
                output[(size_t)(12+y*2+dy)*pitch+12+i*12+x*2+dx]=0xffffffff;
}
static void draw_indicators(uint32_t *output,size_t pitch) {
    if (screen_layout!=2) { fps_counter(output,pitch); draw_toast(output,pitch); return; }
    if (!show_fps && !toast_frames) return;
    static uint32_t rotated[SWITCH_SCREEN_WIDTH*148];
    for (unsigned i=0;i<SWITCH_SCREEN_WIDTH*148;++i) rotated[i]=0;
    fps_counter(rotated,SWITCH_SCREEN_WIDTH); draw_toast(rotated,SWITCH_SCREEN_WIDTH);
    for (int y=0;y<148;++y) for (int x=0;x<736;++x) {
        uint32_t color=rotated[y*SWITCH_SCREEN_WIDTH+x];
        int target_y=y>=32 ? x-20 : x;
        if (color && target_y>=0 && target_y<720) output[(size_t)target_y*pitch+1279-y]=color;
    }
}

bool SwitchOverlay_Compose(const SwitchOverlay *overlay, uint32_t *output,
                           size_t output_pitch, const SwitchSurface *main_screen,
                           const SwitchSurface *second_screen) {
    if (!overlay || !output || output_pitch < SWITCH_SCREEN_WIDTH ||
        !valid(main_screen) || ((overlay->visible || screen_layout) && !valid(second_screen))) return false;
    for (int y = 0; y < SWITCH_SCREEN_HEIGHT; ++y)
        for (int x = 0; x < SWITCH_SCREEN_WIDTH; ++x)
            output[(size_t)y * output_pitch + x] = 0xff000000;
    if (screen_layout==2) {
        int w=760,h=720;
        if (screen_scaling==0) {
            int zx=w/main_screen->height,zy=h/main_screen->width,zoom=zx<zy ? zx : zy;
            if (zoom<1) return false;
            w=main_screen->height*zoom; h=main_screen->width*zoom;
        } else if (screen_scaling==1) {
            h=w*main_screen->width/main_screen->height;
            if (h>720) { h=720; w=h*main_screen->height/main_screen->width; }
        }
        scale_clockwise(output,output_pitch,main_screen,520+(760-w)/2,(720-h)/2,w,h);
        for (int y=36;y<684;++y) for (int x=12;x<500;++x)
            output[(size_t)y*output_pitch+x]=overlay->visible ? 0xffe0b050 : 0xff706050;
        scale_clockwise(output,output_pitch,second_screen,16,40,480,640);
        draw_indicators(output,output_pitch);
        return true;
    }
    int area_width=screen_layout ? 800 : SWITCH_SCREEN_WIDTH;
    int area_height=SWITCH_SCREEN_HEIGHT;
    int w=area_width,h=area_height;
    if (screen_scaling==0) {
        int zoom_x=area_width/main_screen->width,zoom_y=area_height/main_screen->height;
        int zoom=zoom_x<zoom_y ? zoom_x : zoom_y;
        if (zoom<1) return false;
        w=main_screen->width*zoom; h=main_screen->height*zoom;
    } else if (screen_scaling==1) {
        h=w*main_screen->height/main_screen->width;
        if (h>area_height) { h=area_height; w=h*main_screen->width/main_screen->height; }
    }
    scale(output,output_pitch,main_screen,(area_width-w)/2,(area_height-h)/2,w,h);
    if (!overlay->visible && !screen_layout) { draw_indicators(output,output_pitch); return true; }
    int left,top,width,height;
    SwitchOverlay_SecondBounds(&left,&top,&width,&height);
    if (!screen_layout) {
        for (int y=0;y<SWITCH_SCREEN_HEIGHT;++y) {
            for (int x=0;x<SWITCH_SCREEN_WIDTH;++x) {
                uint32_t *pixel=&output[(size_t)y*output_pitch+x];
                *pixel=((*pixel&0x00fefefe)>>1)|0xff000000;
            }
        }
    }
    for (int y=top-4;y<top+height+4;++y)
        for (int x=left-4;x<left+width+4;++x)
            output[(size_t)y*output_pitch+x]=overlay->visible ? 0xffe0b050 : 0xff706050;
    scale(output,output_pitch,second_screen,left,top,width,height);
    draw_indicators(output,output_pitch);
    return true;
}
