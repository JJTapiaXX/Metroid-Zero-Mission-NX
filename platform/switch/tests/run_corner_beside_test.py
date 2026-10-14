#!/usr/bin/env python3
"""Verify native/Wide Corner Beside geometry in all four scale modes."""
from pathlib import Path
s=Path(__file__).with_name('run_game_smoke.py').read_text()
exec(compile(s.split('try:\n    path=allocate(16)')[0],'helpers','exec'))
p=allocate(16);uc.mem_write(p,b'mzm.gba\0');assert call('GameSwitch_Load',p)
main=allocate(24);secondary=allocate(24);overlay=allocate(2);output=allocate(1280*720*4);bounds=allocate(16)
source=allocate(288*160*4);uc.mem_write(source,struct.pack('<I',0xff8899aa)*(288*160))
ui=allocate(320*240*4);uc.mem_write(ui,struct.pack('<I',0xff223344)*(320*240));uc.mem_write(secondary,struct.pack('<QiiQ',ui,320,240,320));uc.mem_write(overlay,b'\0\0')
call('SwitchOverlay_SetLcd',0)
for source_width in (240,288):
    uc.mem_write(main,struct.pack('<QiiQ',source,source_width,160,source_width))
    for scale in range(4):
        call('SwitchOverlay_SetOptions',4,scale);assert call('SwitchOverlay_Compose',overlay,output,1280,main,secondary)
        call('SwitchOverlay_SecondBounds',bounds,bounds+4,bounds+8,bounds+12)
        x,y,w,h=struct.unpack('<4i',uc.mem_read(bounds,16))
        game_height=720 if scale==2 else 160*min(960//source_width,720//160) if scale==0 else 960*160//source_width
        game_top=(720-game_height)//2
        assert x-4>=960 and y+h+4==game_top+game_height
        pixels=uc.mem_read(output,1280*720*4)
        assert struct.unpack_from('<I',pixels,((game_top+1)*1280)*4)[0]==0xff8899aa
        assert struct.unpack_from('<I',pixels,(y*1280+x)*4)[0]==0xff223344
        assert struct.unpack_from('<I',pixels,(game_top*1280+1000)*4)[0]==0xff000000
print('PASS: left gameplay, separate lower-right screen, exact border alignment for all eight viewport/scale combinations',flush=True)
