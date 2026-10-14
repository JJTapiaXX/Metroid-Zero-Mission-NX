#!/usr/bin/env python3
"""Verify Flip 270 geometry, touch, indicators, filters and scaling."""
from pathlib import Path
s=Path(__file__).with_name('run_game_smoke.py').read_text();exec(compile(s.split('try:\n    path=allocate(16)')[0],'helpers','exec'))
p=allocate(16);uc.mem_write(p,b'mzm.gba\0');assert call('GameSwitch_Load',p)
source=allocate(240*160*4);uc.mem_write(source,b''.join(struct.pack('<I',0xff000000|(x<<8)|y) for y in range(160) for x in range(240)))
ui=allocate(320*240*4);uc.mem_write(ui,struct.pack('<I',0xffaabbcc)*(320*240))
a=allocate(24);b=allocate(24);ov=allocate(2);out=allocate(1280*720*4);coords=allocate(16)
uc.mem_write(a,struct.pack('<QiiQ',source,240,160,240));uc.mem_write(b,struct.pack('<QiiQ',ui,320,240,320));uc.mem_write(ov,b'\0\0')
call('SwitchOverlay_SetOptions',5,0);call('SwitchOverlay_SetLcd',0)
assert call('SwitchOverlay_Compose',ov,out,1280,a,b)
def pixel(x,y):return struct.unpack('<I',uc.mem_read(out+(y*1280+x)*4,4))[0]
assert pixel(140,0)==0xff00ef00 and pixel(619,719)==0xff00009f
assert pixel(784,40)==0xffaabbcc and pixel(1263,679)==0xffaabbcc
assert call('SwitchOverlay_MapPoint',ov,784,40,coords,coords+4)
assert struct.unpack('<ii',uc.mem_read(coords,8))==(319,0)
assert call('SwitchOverlay_MapPoint',ov,1263,679,coords,coords+4)
assert struct.unpack('<ii',uc.mem_read(coords,8))==(0,239)
call('SwitchOverlay_ShowFps',1);call('SwitchOverlay_SetFps',60);assert call('SwitchOverlay_Compose',ov,out,1280,a,b);assert pixel(12,707)==0xffffffff
call('SwitchOverlay_Toast',allocate(1),allocate(1));assert call('SwitchOverlay_Compose',ov,out,1280,a,b);assert pixel(32,715)==0xff80e080
for scale in (1,2,3):
 call('SwitchOverlay_SetOptions',5,scale)
 for filter in (0,1,2,3):
  call('SwitchOverlay_SetLcd',filter);assert call('SwitchOverlay_Compose',ov,out,1280,a,b);assert pixel(784,40)==0xffaabbcc
print('PASS: Flip 270 main orientation, swapped screen positions, inverse touch mapping, rotated FPS/toast and every scale/filter',flush=True)
