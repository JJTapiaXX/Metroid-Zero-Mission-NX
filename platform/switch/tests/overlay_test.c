/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "screen_overlay.h"
#include <string.h>

void *memcpy(void *dst,const void *src,size_t count) { unsigned char *d=dst; const unsigned char *s=src; while(count--) *d++=*s++; return dst; }
static uint32_t main_pixels[240 * 160];
static uint32_t bottom_pixels[320 * 240];
/* Include row padding to catch mistaken byte/pixel strides. */
static uint32_t output[1288 * 720];

/* Freestanding entry point, run using the same AArch64 object code as Switch. */
int overlay_test(void) {
    SwitchOverlay overlay = {0};
    SwitchSurface main_screen = {main_pixels, 240, 160, 240};
    SwitchSurface second_screen = {bottom_pixels, 320, 240, 320};
    int x = -1, y = -1;
    if (SwitchOverlay_MapPoint(&overlay, 192, 24, &x, &y)) return 1;
    SwitchOverlay_Update(&overlay, true);
    if (!overlay.visible) return 2;
    for (int i = 0; i < 100; ++i) SwitchOverlay_Update(&overlay, true);
    if (!overlay.visible) return 3;
    if (!SwitchOverlay_MapPoint(&overlay, 192, 24, &x, &y) || x != 0 || y != 0) return 4;
    if (!SwitchOverlay_MapPoint(&overlay, 1087, 695, &x, &y) || x != 319 || y != 239) return 5;
    if (!SwitchOverlay_MapPoint(&overlay, 640, 360, &x, &y) || x != 160 || y != 120) return 6;
    if (SwitchOverlay_MapPoint(&overlay, 191, 24, &x, &y) ||
        SwitchOverlay_MapPoint(&overlay, 192, 23, &x, &y) ||
        SwitchOverlay_MapPoint(&overlay, 1088, 24, &x, &y) ||
        SwitchOverlay_MapPoint(&overlay, 192, 696, &x, &y)) return 7;
    for (int i = 0; i < 240 * 160; ++i) main_pixels[i] = 0xff224466;
    for (int i = 0; i < 320 * 240; ++i) bottom_pixels[i] = 0xff99aabb;
    for (int i = 0; i < 1288 * 720; ++i) output[i] = 0xdeadbeef;
    if (!SwitchOverlay_Compose(&overlay, output, 1288, &main_screen, &second_screen)) return 8;
    if (output[24 * 1288 + 192] != 0xff99aabb ||
        output[695 * 1288 + 1087] != 0xff99aabb) return 9;
    if (output[100 * 1288 + 180] != 0xff112233) return 10;
    if (output[20 * 1288 + 188] != 0xffe0b050) return 11;
    for (int row = 0; row < 720; ++row)
        for (int column = 1280; column < 1288; ++column)
            if (output[row * 1288 + column] != 0xdeadbeef) return 12;
    SwitchOverlay_Update(&overlay, false);
    SwitchOverlay_Update(&overlay, true);
    if (overlay.visible) return 13;
    if (!SwitchOverlay_Compose(&overlay, output, 1288, &main_screen, 0)) return 14;
    if (output[100 * 1288 + 200] != 0xff224466 || output[0] != 0xff000000) return 15;
    if (SwitchOverlay_Compose(&overlay, output, 100, &main_screen, &second_screen)) return 16;
    SwitchOverlay_SetOptions(1,0);
    if (!SwitchOverlay_Compose(&overlay,output,1288,&main_screen,&second_screen)) return 17;
    if (output[192*1288+816]!=0xff99aabb || output[120*1288+40]!=0xff224466) return 18;
    if (!SwitchOverlay_MapPoint(&overlay,1040,360,&x,&y) || x!=160 || y!=120) return 19;
    if (!SwitchOverlay_MapPoint(&overlay,1263,527,&x,&y) || x!=319 || y!=239) return 20;
    if (SwitchOverlay_MapPoint(&overlay,1264,527,&x,&y)) return 21;
    for (unsigned scale=1;scale<=2;++scale) {
        SwitchOverlay_SetOptions(1,scale);
        if (!SwitchOverlay_Compose(&overlay,output,1288,&main_screen,&second_screen)) return 22;
        if (output[360*1288+400]!=0xff224466) return 23;
    }
    for (int row=0;row<720;++row) for (int column=1280;column<1288;++column)
        if (output[row*1288+column]!=0xdeadbeef) return 24;
    SwitchOverlay_ShowFps(true); SwitchOverlay_SetFps(60);
    if (!SwitchOverlay_Compose(&overlay,output,1288,&main_screen,&second_screen)) return 25;
    if (output[12*1288+12]!=0xffffffff) return 26;
    SwitchOverlay_ShowFps(false);
    for (int y=0;y<160;++y) for (int x=0;x<240;++x) main_pixels[y*240+x]=0xff000000u|((unsigned)x<<8)|(unsigned)y;
    for (unsigned layout=0;layout<2;++layout) for (unsigned scaling=0;scaling<3;++scaling) {
        SwitchOverlay_SetOptions(layout,scaling);
        if (!SwitchOverlay_Compose(&overlay,output,1288,&main_screen,&second_screen)) return 27;
        int area=layout ? 800 : 1280,w=area,h=720;
        if (scaling==0) { int factor=layout ? 3 : 4; w=240*factor; h=160*factor; }
        else if (scaling==1) { h=w*160/240; if (h>720) { h=720; w=h*240/160; } }
        int left=(area-w)/2,top=(720-h)/2;
        for (int y=0;y<720;y+=13) for (int x=0;x<area;x+=17) {
            uint32_t expected=0xff000000u;
            if (x>=left && x<left+w && y>=top && y<top+h)
                expected=main_pixels[((y-top)*160/h)*240+(x-left)*240/w];
            if (output[y*1288+x]!=expected) return 28;
        }
    }
    SwitchOverlay_SetOptions(0,0);
    SwitchOverlay_SetOptions(2,0);
    if (!SwitchOverlay_Compose(&overlay,output,1288,&main_screen,&second_screen)) return 33;
    if (output[0*1288+660]!=main_pixels[159*240]) return 34;
    if (output[719*1288+1139]!=main_pixels[239]) return 35;
    if (!SwitchOverlay_MapPoint(&overlay,16,40,&x,&y) || x!=0 || y!=239) return 36;
    if (!SwitchOverlay_MapPoint(&overlay,495,679,&x,&y) || x!=319 || y!=0) return 37;
    if (SwitchOverlay_MapPoint(&overlay,496,680,&x,&y)) return 38;
    SwitchOverlay_SetOptions(0,0);
    static uint32_t badge[400]; for (int i=0;i<400;++i) badge[i]=0xff123456;
    SwitchOverlay_Toast("Morph Ball","5 POINTS EARNED"); SwitchOverlay_ToastBadge(badge);
    if (!SwitchOverlay_Compose(&overlay,output,1288,&main_screen,&second_screen)) return 29;
    if (output[50*1288+40]!=0xff123456 || output[129*1288+119]!=0xff123456) return 30;
    if (output[32*1288+24]!=0xff80e080) return 31;
    for (int row=0;row<720;++row) for (int column=1280;column<1288;++column)
        if (output[row*1288+column]!=0xdeadbeef) return 32;
    SwitchOverlay_SetOptions(2,0);
    if (!SwitchOverlay_Compose(&overlay,output,1288,&main_screen,&second_screen)) return 39;
    if (output[4*1288+1247]!=0xff80e080 || output[20*1288+1229]!=0xff123456) return 40;
    SwitchOverlay_ShowFps(false);
    for (unsigned layout=3;layout<=4;++layout) for (unsigned scaling=0;scaling<4;++scaling) {
        SwitchOverlay_SetOptions(layout,scaling);
        int left,top,width,height;
        if (!SwitchOverlay_Compose(&overlay,output,1288,&main_screen,&second_screen)) return 47;
        SwitchOverlay_SecondBounds(&left,&top,&width,&height);
        int area=layout==4 ? 960:1280,h=720;
        if (scaling==0) h=160*(area/240<720/160 ? area/240:720/160);
        else if (scaling==1 || scaling==3) { h=area*160/240; if(h>720)h=720; }
        if (layout==4 && top+height+4!=(720-h)/2+h) return 48;
        if (layout==3 && top-4!=(720-h)/2) return 49;
        if (!SwitchOverlay_MapPoint(&overlay,left+width/2,top+height/2,&x,&y) || x!=160 || y!=120) return 41;
        if (SwitchOverlay_MapPoint(&overlay,left+width,top,&x,&y)) return 42;
        if (!SwitchOverlay_Compose(&overlay,output,1288,&main_screen,&second_screen)) return 43;
        if (output[top*1288+left]!=0xff99aabb || output[(top+height-1)*1288+left+width-1]!=0xff99aabb) return 44;
        for (int row=0;row<720;++row) for (int column=1280;column<1288;++column)
            if (output[row*1288+column]!=0xdeadbeef) return 46;
    }
    SwitchOverlay_SetOptions(4,1);
    if(!SwitchOverlay_Compose(&overlay,output,1288,&main_screen,&second_screen))return 56;
    if(output[200*1288]!=main_pixels[40*240] || output[200*1288+1000]!=0xff000000)return 57;
    SwitchOverlay_SetOptions(3,1);
    SwitchOverlay_SetCornerPlacement(true,800,100,320);
    bool held,pressed,save; int px,py,left,top,width,height;
    SwitchOverlay_CornerTouch(1,900,180,0,0,1000,&px,&py,&held,&pressed,&save);
    if(held || pressed) return 50;
    SwitchOverlay_CornerTouch(0,0,0,0,0,1050,&px,&py,&held,&pressed,&save);
    SwitchOverlay_CornerTouch(1,900,180,0,0,1100,&px,&py,&held,&pressed,&save);
    SwitchOverlay_CornerTouch(1,800,280,0,0,1150,&px,&py,&held,&pressed,&save);
    SwitchOverlay_SecondBounds(&left,&top,&width,&height);
    if(left!=700 || top!=200 || held) return 51;
    SwitchOverlay_CornerTouch(0,0,0,0,0,1200,&px,&py,&held,&pressed,&save);
    if(!save) return 52;
    SwitchOverlay_CornerTouch(2,750,250,850,250,1300,&px,&py,&held,&pressed,&save);
    SwitchOverlay_CornerTouch(2,700,250,900,250,1350,&px,&py,&held,&pressed,&save);
    SwitchOverlay_SecondBounds(&left,&top,&width,&height);
    if(width!=640 || height!=480 || held || left<4 || top<4) return 53;
    SwitchOverlay_CornerTouch(0,0,0,0,0,1400,&px,&py,&held,&pressed,&save);
    if(!save) return 54;
    SwitchOverlay_CornerTouch(1,left+20,top+20,0,0,2000,&px,&py,&held,&pressed,&save);
    SwitchOverlay_CornerTouch(0,0,0,0,0,2050,&px,&py,&held,&pressed,&save);
    SwitchOverlay_CornerTouch(0,0,0,0,0,2400,&px,&py,&held,&pressed,&save);
    if(!held || !pressed || px!=left+20 || py!=top+20) return 55;
    SwitchOverlay_SetCornerPlacement(false,4,4,312);
    return 0;
}
