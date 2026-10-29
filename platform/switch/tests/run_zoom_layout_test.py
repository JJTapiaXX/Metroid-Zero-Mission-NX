"""Verify all layouts and filters with the 320x192 Zoom Out surface."""
from pathlib import Path
source=Path(__file__).with_name('run_game_smoke.py').read_text();exec(compile(source.split("try:\n    path=allocate(16)")[0],'helpers','exec'))
p=allocate(16);uc.mem_write(p,b'mzm.gba\0');assert call('GameSwitch_Load',p)
a=allocate(24);b=allocate(24);ov=allocate(2);target=allocate(1280*720*4);scene=allocate(320*192*4);ui=allocate(320*240*4);bounds=allocate(16)
uc.mem_write(scene,struct.pack('<I',0xff886644)*(320*192));uc.mem_write(ui,struct.pack('<I',0xff223344)*(320*240));uc.mem_write(a,struct.pack('<QiiQ',scene,320,192,320));uc.mem_write(b,struct.pack('<QiiQ',ui,320,240,320));uc.mem_write(ov,b'\1\0')
for layout in range(6):
 call('SwitchOverlay_SetOptions',layout,4);golden=None
 for filter in (0,1,2,3,0):
  call('SwitchOverlay_SetLcd',filter);assert call('SwitchOverlay_Compose',ov,target,1280,a,b)
  pixels=bytes(uc.mem_read(target,1280*720*4))
  if filter==0:
   if golden is None:golden=pixels
   else:assert golden==pixels
  else:assert pixels!=golden,('filter missing on 320-wide gameplay',layout,filter)
  call('SwitchOverlay_SecondBounds',bounds,bounds+4,bounds+8,bounds+12);x,y,w,h=struct.unpack('<4i',uc.mem_read(bounds,16))
  assert struct.unpack_from('<I',pixels,(y*1280+x)*4)[0]==0xff223344,('UI filtered',layout,filter)
 print('PASS: Zoom Out layout',layout,'all filters; 320-wide gameplay distinguished from 320-wide UI',flush=True)

# Verify the actual frontend forwards the taller frame to presentation.
expected=[(320,192,320)]
def poll():
 uc.mem_write(uc.reg_read(UC_ARM64_REG_X8),struct.pack('<HBBBBxxiiBxxx',0,0,1,0,0,0,0,0));return_value()
def present():
 _,width,height,pitch=struct.unpack('<QiiQ',uc.mem_read(uc.reg_read(UC_ARM64_REG_X0),24))
 assert (width,height,pitch)==expected[0],('frontend surface geometry',width,height,pitch)
 return_value(1)
hook('PlatformSwitch_Poll',poll);hook('PlatformSwitch_Present',present);hook('CallbackCallVblank',lambda:return_value());hook('virtuappu_render_frame',lambda:return_value())
for name,value in [('gMainGameMode',4),('gSubGameMode1',2)]:uc.mem_write(symbols[name],struct.pack('<H',value))
for name in ('gDisableDrawingSamusAndScrolling','gColorFading'):uc.mem_write(symbols[name],b'\0')
uc.mem_write(symbols['gInGameCutscene']+3,b'\0');uc.mem_write(symbols['gIoMem'],bytes(2))
for layout in (1,2,3,4,5):
 call('SwitchOverlay_SetOptions',layout,4);call('GameSwitch_VBlank')
uc.mem_write(symbols['gMainGameMode'],struct.pack('<H',1));expected[0]=(240,160,240);call('GameSwitch_VBlank')
print('PASS: real frontend presents 320x192 gameplay and returns to 240x160 menus',flush=True)

for layout in (2,5):
 for scale in (1,2):
  call('SwitchOverlay_SetOptions',layout,scale)
  assert call('SwitchOverlay_Scaling')==0,'Flip Grip accepted Fit/Stretch'
 for scale in (0,3,4):
  call('SwitchOverlay_SetOptions',layout,scale)
  assert call('SwitchOverlay_Scaling')==scale
print('PASS: both Flip Grip modes support Pixel Perfect, Wide, Zoom Out only',flush=True)
