/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "screen_overlay.h"
#include <string.h>
#include <arm_neon.h>



static unsigned screen_layout,screen_scaling,display_fps;
static bool show_fps;
static unsigned lcd_filter;
unsigned SwitchOverlay_Lcd(void) { return lcd_filter; }
void SwitchOverlay_SetLcd(unsigned filter) { lcd_filter=filter>3 ? 3:filter; }
static uint32_t lcd_dim(uint32_t color) { return color-((color&0x00f8f8f8u)>>3); }
static int main_width=240,main_height=160;
static void main_bounds(int *left,int *top,int *width,int *height) {
    int area=screen_layout==1 ? 800:screen_layout==4 ? 960:SWITCH_SCREEN_WIDTH;
    int w=area,h=720;
    if (screen_scaling==0) {
        int zx=area/main_width,zy=720/main_height,zoom=zx<zy ? zx:zy;
        w=main_width*zoom; h=main_height*zoom;
    } else if (screen_scaling==1 || screen_scaling>=3) {
        h=w*main_height/main_width;
        if (h>720) { h=720; w=h*main_width/main_height; }
    }
    *left=screen_layout==4 ? 0:(area-w)/2; *top=(720-h)/2; *width=w; *height=h;
}
static int main_bottom(void) {
    int left,top,width,height;
    main_bounds(&left,&top,&width,&height);
    return top+height;
}
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
    screen_layout=layout>5 ? 5 : layout; screen_scaling=scaling>4 ? 4 : scaling;
    if ((screen_layout==2 || screen_layout==5) && (screen_scaling==1 || screen_scaling==2)) screen_scaling=0;
}
static bool corner_custom;
static int corner_x,corner_y,corner_width=312;
bool SwitchOverlay_CornerPlacement(int *x,int *y,int *width) {
    *x=corner_x; *y=corner_y; *width=corner_width; return corner_custom;
}
void SwitchOverlay_SetCornerPlacement(bool enabled,int x,int y,int width) {
    if (width<256) width=256;
    if (width>640) width=640;
    int height=width*3/4;
    if (x<4) x=4;
    if (y<4) y=4;
    if (x>1276-width) x=1276-width;
    if (y>716-height) y=716-height;
    corner_x=x; corner_y=y; corner_width=width; corner_custom=enabled;
}
void SwitchOverlay_SecondBounds(int *x,int *y,int *width,int *height) {
    if (screen_layout==3 && corner_custom) {
        *x=corner_x; *y=corner_y; *width=corner_width; *height=corner_width*3/4; return;
    }
    if (screen_layout==3) {
        int left,top,w,h;
        main_bounds(&left,&top,&w,&h);
        *width=312; *height=234; *x=left+w-*width-4; *y=top+4; return;
    }
    if (screen_layout==4) { *x=964; *y=main_bottom()-234-4; *width=312; *height=234; return; }
    if (screen_layout==5) { *x=784; *y=40; *width=480; *height=640; return; }
    if (screen_layout==2) { *x=16; *y=40; *width=480; *height=640; return; }
    *x=screen_layout ? 816 : 192; *y=screen_layout ? 192 : 24;
    *width=screen_layout ? 448 : 896; *height=screen_layout ? 336 : 672;
}
/* Delay short taps until the double-tap window closes, so gestures cannot
 * activate the underlying tabs, map zoom, or destructive state controls. */
bool SwitchOverlay_CornerTouch(unsigned count,int x,int y,int x2,int y2,uint64_t now,
                               int *point_x,int *point_y,bool *held,bool *pressed,bool *save) {
    static unsigned previous,mode;
    static bool pending,normal,dirty;
    static uint64_t started,released;
    static int tap_x,tap_y,start_x,start_y,left,top,width,distance;
    *held=false; *pressed=false; *save=false;
    if (screen_layout!=3) { previous=mode=0; pending=normal=dirty=false; return false; }
    if (!count) {
        if (previous) {
            if (mode) { *save=dirty; dirty=false; pending=false; }
            else if (!normal) { pending=true; released=now; }
            previous=mode=0; normal=false;
        }
        if (pending && now-released>=300) {
            *point_x=tap_x; *point_y=tap_y; *held=*pressed=true; pending=false;
        }
        return true;
    }
    int bx,by,bw,bh;
    SwitchOverlay_SecondBounds(&bx,&by,&bw,&bh);
    bool inside=x>=bx && x<bx+bw && y>=by && y<by+bh;
    if (!previous) {
        started=now; normal=false;
        int dx=x-tap_x,dy=y-tap_y;
        if (pending && now-released<300 && dx*dx+dy*dy<1600 && inside) {
            mode=1; left=bx; top=by; width=bw; start_x=x; start_y=y;
            pending=false;
        } else { pending=false; mode=inside ? 0:3; tap_x=x; tap_y=y; }
    }
    if (count>=2 && (mode==1 || inside) && mode!=2) {
        mode=2; pending=false; left=bx; top=by; width=bw;
        start_x=(x+x2)/2; start_y=(y+y2)/2;
        int dx=x-x2,dy=y-y2; if (dx<0)dx=-dx; if(dy<0)dy=-dy;
        distance=dx+dy; if(distance<1)distance=1;
    }
    if (mode==1) {
        SwitchOverlay_SetCornerPlacement(true,left+x-start_x,top+y-start_y,width); dirty=true;
    } else if (mode==2) {
        if (count>=2) {
            int dx=x-x2,dy=y-y2; if(dx<0)dx=-dx; if(dy<0)dy=-dy;
            int new_width=width*(dx+dy)/distance;
            if(new_width<256)new_width=256;
            if(new_width>640)new_width=640;
            SwitchOverlay_SetCornerPlacement(true,(x+x2)/2-(start_x-left)*new_width/width,
                (y+y2)/2-(start_y-top)*new_width/width,new_width); dirty=true;
        }
    } else if (mode==0 && now-started>=300) {
        *point_x=x; *point_y=y; *held=true; *pressed=!normal; normal=true;
    }
    previous=count; return true;
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
    if (screen_layout==5) {
        *ui_x=SWITCH_UI_WIDTH-1-(y-top)*SWITCH_UI_WIDTH/height;
        *ui_y=(x-left)*SWITCH_UI_HEIGHT/width; return true;
    }
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

/* Shade four packed RGBA pixels at once. Edge masks are cached with the
 * scaling coordinates. Both rows keep the original scalar filter colors. */
static void filter_rows(uint32_t *normal,uint32_t *dark,const uint32_t *edges,
                        int width,bool grid,bool rotated) {
    int x=0;
    uint32x4_t mask=vdupq_n_u32(grid ? 0x00f8f8f8u:0x00fcfcfcu);
    for(;x+4<=width;x+=4) {
        uint32x4_t color=vld1q_u32(normal+x),bits=vandq_u32(color,mask);
        uint32x4_t shaded=vsubq_u32(color,grid ? vshrq_n_u32(bits,3):vshrq_n_u32(bits,2));
        uint32x4_t base=(grid || rotated) ? vbslq_u32(vld1q_u32(edges+x),shaded,color):color;
        vst1q_u32(normal+x,base);
        if(grid && !rotated) shaded=vsubq_u32(base,vshrq_n_u32(vandq_u32(base,mask),3));
        vst1q_u32(dark+x,shaded);
    }
    for(;x<width;++x) {
        uint32_t color=normal[x],shaded=grid ? lcd_dim(color):color-((color&0x00fcfcfcu)>>2);
        normal[x]=(grid || rotated) && edges[x] ? shaded:color;
        dark[x]=grid && !rotated ? lcd_dim(normal[x]):shaded;
    }
}
static void scale(uint32_t *output,size_t pitch,const SwitchSurface *s,
                  int left,int top,int width,int height) {
    static int lookup[2][SWITCH_SCREEN_WIDTH],cached_width[2],cached_source[2];
    static uint32_t edges[SWITCH_SCREEN_WIDTH],rows[2][SWITCH_SCREEN_WIDTH];
    unsigned slot=s->width==SWITCH_UI_WIDTH && s->height==SWITCH_UI_HEIGHT ? 1:0;
    bool grid=lcd_filter==2 && !slot,scanlines=lcd_filter==3 && !slot;
    if(cached_width[slot]!=width || cached_source[slot]!=s->width) {
        for(int x=0;x<width;++x) {
            lookup[slot][x]=x*s->width/width;
            if(!slot) edges[x]=x && lookup[slot][x]!=lookup[slot][x-1] ? ~0u:0;
        }
        cached_width[slot]=width;cached_source[slot]=s->width;
    }
    int previous_y=-1;
    uint32_t *previous_row=NULL;
    for(int y=0;y<height;++y) {
        int source_y=y*s->height/height;
        uint32_t *dst=output+(size_t)(top+y)*pitch+left;
        if(grid || scanlines) {
            if(source_y!=previous_y) {
                const uint32_t *row=s->pixels+(size_t)source_y*s->pitch;
                for(int x=0;x<width;++x) rows[0][x]=row[lookup[slot][x]];
                filter_rows(rows[0],rows[1],edges,width,grid,false);
            }
            memcpy(dst,rows[scanlines ? (y&1):source_y!=previous_y ? 1:0],(size_t)width*sizeof(*dst));
        } else if(source_y==previous_y) memcpy(dst,previous_row,(size_t)width*sizeof(*dst));
        else {
            const uint32_t *row=s->pixels+(size_t)source_y*s->pitch;
            for(int x=0;x<width;++x) dst[x]=row[lookup[slot][x]];
        }
        previous_y=source_y;previous_row=dst;
    }
}
/* Cache rotated coordinates and expand each source column only once. */
static void scale_clockwise(uint32_t *output,size_t pitch,const SwitchSurface *s,
                            int left,int top,int width,int height) {
    static int lookup[2][SWITCH_SCREEN_WIDTH],cached_width[2],cached_height[2],cached_direction[2],cached_pitch[2];
    static uint32_t edges[2][2][SWITCH_SCREEN_WIDTH],rows[2][SWITCH_SCREEN_WIDTH];
    unsigned slot=s->width==SWITCH_UI_WIDTH && s->height==SWITCH_UI_HEIGHT ? 1:0;
    bool reverse=screen_layout==5,grid=lcd_filter==2 && !slot,scanlines=lcd_filter==3 && !slot;
    if(cached_width[slot]!=width || cached_height[slot]!=s->height || cached_pitch[slot]!=(int)s->pitch || cached_direction[slot]!=(int)reverse+1) {
        for(int x=0;x<width;++x) {
            int row=reverse ? x*s->height/width:s->height-1-x*s->height/width;
            lookup[slot][x]=row*(int)s->pitch;
            edges[slot][0][x]=x && lookup[slot][x]!=lookup[slot][x-1] ? ~0u:0;
            edges[slot][1][x]=(x&1) ? ~0u:0;
        }
        cached_width[slot]=width;cached_height[slot]=s->height;cached_pitch[slot]=(int)s->pitch;cached_direction[slot]=(int)reverse+1;
    }
    int previous_x=-1;
    for(int y=0;y<height;++y) {
        int source_x=reverse ? s->width-1-y*s->width/height:y*s->width/height;
        bool changed=source_x!=previous_x;
        if(changed) {
            for(int x=0;x<width;++x) rows[0][x]=s->pixels[lookup[slot][x]+source_x];
            if(grid || scanlines) filter_rows(rows[0],rows[1],edges[slot][scanlines ? 1:0],width,grid,true);
        }
        /* The old grid left the first rotated row unshaded except at column
         * boundaries, then shaded subsequent source-column boundaries. */
        memcpy(output+(size_t)(top+y)*pitch+left,rows[grid && changed && y ? 1:0],(size_t)width*sizeof(uint32_t));
        previous_x=source_x;
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
    if (screen_layout!=2 && screen_layout!=5) { fps_counter(output,pitch); draw_toast(output,pitch); return; }
    if (!show_fps && !toast_frames) return;
    static uint32_t rotated[SWITCH_SCREEN_WIDTH*148];
    bool toast_visible=toast_frames!=0;
    if (toast_visible) for (int y=32;y<148;++y)
        memset(rotated+(size_t)y*SWITCH_SCREEN_WIDTH+24,0,712*sizeof(uint32_t));
    if (show_fps) for (int y=8;y<30;++y)
        memset(rotated+(size_t)y*SWITCH_SCREEN_WIDTH+8,0,92*sizeof(uint32_t));
    fps_counter(rotated,SWITCH_SCREEN_WIDTH); draw_toast(rotated,SWITCH_SCREEN_WIDTH);
    /* Rotate only the populated indicator rectangles, not a full toast-sized
     * canvas every frame just to display the small FPS counter. */
    for (int y=8;y<(toast_visible ? 148:30);++y) {
      if (y>=30 && y<32) continue;
      if (y<30 && !show_fps) continue;
      int first=y<30 ? 8:24,last=y<30 ? 100:736;
      for (int x=first;x<last;++x) {
        uint32_t color=rotated[y*SWITCH_SCREEN_WIDTH+x];
        int target_y=y>=32 ? x-20 : x;
        if (color && target_y>=0 && target_y<720) output[(size_t)(screen_layout==5 ? 719-target_y:target_y)*pitch+(screen_layout==5 ? y:1279-y)]=color;
    }
    }
}

bool SwitchOverlay_Compose(const SwitchOverlay *overlay, uint32_t *output,
                           size_t output_pitch, const SwitchSurface *main_screen,
                           const SwitchSurface *second_screen) {
    if (!overlay || !output || output_pitch < SWITCH_SCREEN_WIDTH ||
        !valid(main_screen) || ((overlay->visible || screen_layout) && !valid(second_screen))) return false;
    main_width=main_screen->width; main_height=main_screen->height;
    /* Filter only the small gameplay source, never the second-screen UI.
     * Row copies remain the fast path; no full-output postprocessing pass. */
    static uint32_t lcd_source[320*192];
    SwitchSurface filtered;
    if((lcd_filter==1 || lcd_filter==2) && main_width<=320 && main_height<=192) {
        for(int y=0;y<main_height;++y) for(int x=0;x<main_width;++x) {
            uint32_t color=main_screen->pixels[(size_t)y*main_screen->pitch+x];
            lcd_source[y*main_width+x]=0xff101010u+((color&0x00fefefeu)>>1)+
                ((color&0x00fcfcfcu)>>2)+((color&0x00f8f8f8u)>>3);
        }
        filtered=(SwitchSurface){lcd_source,main_width,main_height,(size_t)main_width};
        main_screen=&filtered;
    }
    for (int y = 0; y < SWITCH_SCREEN_HEIGHT; ++y)
        for (int x = 0; x < SWITCH_SCREEN_WIDTH; ++x)
            output[(size_t)y * output_pitch + x] = 0xff000000;
    if (screen_layout==2 || screen_layout==5) {
        int w=760,h=720;
        if (screen_scaling==0) {
            int zx=w/main_screen->height,zy=h/main_screen->width,zoom=zx<zy ? zx : zy;
            if (zoom<1) return false;
            w=main_screen->height*zoom; h=main_screen->width*zoom;
        } else if ((screen_scaling==1 || screen_scaling>=3)) {
            h=w*main_screen->width/main_screen->height;
            if (h>720) { h=720; w=h*main_screen->height/main_screen->width; }
        }
        scale_clockwise(output,output_pitch,main_screen,(screen_layout==5 ? 0:520)+(760-w)/2,(720-h)/2,w,h);
        int second_left=screen_layout==5 ? 784:16;
        for (int y=36;y<684;++y) for (int x=second_left-4;x<second_left+484;++x)
            output[(size_t)y*output_pitch+x]=overlay->visible ? 0xffe0b050 : 0xff706050;
        scale_clockwise(output,output_pitch,second_screen,second_left,40,480,640);
        draw_indicators(output,output_pitch);
        return true;
    }
    int main_left,main_top,w,h;
    main_bounds(&main_left,&main_top,&w,&h);
    if (w<1 || h<1) return false;
    scale(output,output_pitch,main_screen,main_left,main_top,w,h);
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
    for (int y=top-4;y<top+height+4 && y<SWITCH_SCREEN_HEIGHT;++y)
        for (int x=left-4;x<left+width+4;++x)
            output[(size_t)y*output_pitch+x]=overlay->visible ? 0xffe0b050 : 0xff706050;
    scale(output,output_pitch,second_screen,left,top,width,height);
    draw_indicators(output,output_pitch);
    return true;
}
