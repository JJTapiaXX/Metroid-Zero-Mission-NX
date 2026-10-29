"""Check true four-sided scenery against native room renders."""
from pathlib import Path
source=Path(__file__).with_name('run_game_smoke.py').read_text()
exec(compile(source.split("try:\n    path=allocate(16)")[0],'smoke_helpers','exec'))
path=allocate(16);uc.mem_write(path,b'mzm.gba\0');assert call('GameSwitch_Load',path)
for name,value,size in [('gCurrentArea',2,1),('gMainGameMode',4,2),('gSubGameMode1',2,2)]:uc.mem_write(symbols[name],value.to_bytes(size,'little'))
uc.mem_write(symbols['gInGameCutscene']+3,b'\0');uc.mem_write(symbols['gDisableDrawingSamusAndScrolling'],b'\0');uc.mem_write(symbols['gColorFading'],b'\0')
uc.mem_write(symbols['gOamMem'],struct.pack('<4H',0x200,0,0,0)*128)
from PIL import Image
for room in range(4):
 uc.mem_write(symbols['gCurrentRoom'],bytes([room]));call('RoomLoadEntry');call('RoomLoadTileset');call('RoomLoadBackgrounds')
 dims=[struct.unpack('<QHH',uc.mem_read(symbols['gBgPointersAndDimensions']+i*16,12)) for i in range(3)]
 rw,rh=dims[1][1]*16,dims[1][2]*16
 camera_x=max(0,min(128,rw-280));camera_y=max(0,min(64,rh-176))
 def camera(x,y):
  for bg in range(3):
   uc.mem_write(symbols[f'gBg{bg}XPosition'],struct.pack('<H',x*4));uc.mem_write(symbols[f'gBg{bg}YPosition'],struct.pack('<H',y*4));call('RoomSetInitialTilemap',bg)
  io=bytearray(0x400);struct.pack_into('<H',io,0,0x700)
  for bg in range(3):
   struct.pack_into('<H',io,8+bg*2,0x4004|(bg*2<<8)|bg);struct.pack_into('<HH',io,16+bg*4,x&511,y&511)
  uc.mem_write(symbols['gIoMem'],bytes(io));uc.mem_write(symbols['virtuappu_registers']+4,b'\x01')
 def render(scale):
  call('SwitchOverlay_SetOptions',0,scale);call('SwitchWide_Prepare');call('SwitchWide_CenterBegin');call('virtuappu_render_frame');call('SwitchWide_CenterEnd')
  return bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],(320*192 if scale==4 else 240*160)*4))
 camera(camera_x,camera_y);native=render(1)
 saved_map=bytes(uc.mem_read(symbols['gVram'],0x4000));saved_io=bytes(uc.mem_read(symbols['gIoMem'],0x400))
 zoom=render(4)
 assert call('SwitchWide_FrameHeight')==192
 assert bytes(uc.mem_read(symbols['gVram'],0x4000))==saved_map
 assert bytes(uc.mem_read(symbols['gIoMem'],0x400))==saved_io
 preserved=b''.join(zoom[((y+16)*320+40)*4:((y+16)*320+280)*4] for y in range(160))
 assert native==preserved,('native scene changed',room)
 checked=0
 for dx,dy in ((-40,-16),(40,-16),(-40,16),(40,16)):
  x,y=camera_x+dx,camera_y+dy
  if x<0 or y<0 or x+240>rw or y+160>rh:continue
  camera(x,y);reference=render(1)
  crop=b''.join(zoom[((line+dy+16)*320+dx+40)*4:((line+dy+16)*320+dx+280)*4] for line in range(160))
  assert crop==reference,('extra scenery does not match room',room,dx,dy)
  checked+=1
 Image.frombytes('RGBA',(320,192),zoom).save(args.output/f'zoom-room-{room}.png')
 Image.frombytes('RGBA',(240,160),native).save(args.output/f'native-room-{room}.png')
 print('PASS: room',room,'original pixels, VRAM/IO restoration,',checked,'four-sided scenery references',flush=True)

# True vertical origins distinguish offscreen world parts from parked OAM.
uc.mem_write(symbols['gBgPointersAndDimensions']+24,struct.pack('<HH',64,64))
uc.mem_write(symbols['gBg1XPosition'],struct.pack('<H',512*4));uc.mem_write(symbols['gBg1YPosition'],struct.pack('<H',256*4))
io=bytearray(0x400);struct.pack_into('<H',io,0,0x1000);struct.pack_into('<HH',io,0x14,0,256);uc.mem_write(symbols['gIoMem'],bytes(io))
uc.mem_write(symbols['gBgPltt'],bytes(512));uc.mem_write(symbols['gObjPltt']+30,struct.pack('<H',0x7fff));uc.mem_write(symbols['gVram']+0x10000,bytes([0xff])*0x8000)
call('SwitchHud_BeginFrame');call('SwitchHud_NoteEnd',1);call('SwitchHud_NoteMinimap',1);call('Port_OverlayText_BeginFrame');call('SwitchWorld_BeginFrame')
call('SwitchWorld_NoteSlots',3,4,-8);call('SwitchWorld_NoteSlotsY',3,4,-8)
call('SwitchWorld_NoteSlots',4,5,264);call('SwitchWorld_NoteSlotsY',4,5,164)
call('Port_OverlayText_NoteBannerOam',6,7)
oam=bytearray(struct.pack('<4H',0x200,0,0,0)*128)
for slot,y,x in [(0,0,4),(1,0,210),(2,80,100),(3,248,504),(4,164,264),(5,120,256),(6,72,108),(7,192,100|0xc000)]:struct.pack_into('<4H',oam,slot*8,y,x,0,0)
uc.mem_write(symbols['gOamMem'],bytes(oam));zoom=render(4)
black=struct.pack('<I',0xff000000)
for x,y in [(16,12),(278,12),(140,96),(32,8),(304,180),(148,88)]:assert zoom[(y*320+x)*4:(y*320+x+1)*4]!=black,('Zoom sprite/HUD missing',x,y)
assert zoom[140*4:141*4]==black,'parked 64-pixel object leaked into top margin'
# Minimap's native hidden top/right pixels must remain hidden after relocation.
struct.pack_into('<4H',oam,8,250,214|0x8000,0,0)
uc.mem_write(symbols['gOamMem'],bytes(oam));cropped=render(4)
for x,y in [(282,6),(308,12),(307,12)]:
 pixel=cropped[(y*320+x)*4:(y*320+x+1)*4]
 assert (pixel!=black)==(x==307),('minimap native crop',x,y)
print('PASS: minimap hidden top and right sprite edges stay clipped',flush=True)
print('PASS: world sprites in all four directions, bottom OAM wrap, and top-anchored HUD/minimap',flush=True)
# A native-size room has equal blank margins on every side.
uc.mem_write(symbols['gBgPointersAndDimensions']+24,struct.pack('<HH',15,10));uc.mem_write(symbols['gBg1XPosition'],bytes(2));uc.mem_write(symbols['gBg1YPosition'],bytes(2))
uc.mem_write(symbols['gVram']+0x4020,bytes([0xff])*32);uc.mem_write(symbols['gVram']+0x1000,struct.pack('<H',1)*2048);uc.mem_write(symbols['gBgPltt']+30,struct.pack('<H',0x7fff));uc.mem_write(symbols['gOamMem'],struct.pack('<4H',0x200,0,0,0)*128)
io=bytearray(0x400);struct.pack_into('<H',io,0,0x200);struct.pack_into('<H',io,10,0x4204);uc.mem_write(symbols['gIoMem'],bytes(io));zoom=render(4)
for y in range(192):
 for x in range(320):
  visible=zoom[(y*320+x)*4:(y*320+x+1)*4]!=black
  assert visible==(40<=x<280 and 16<=y<176),('room border incorrect',x,y)
print('PASS: native room centered between equal 40-pixel side borders and 16-pixel vertical borders',flush=True)
# Door fades and affine/cutscene presentations keep the native frame.
for symbol,value in [('gDisableDrawingSamusAndScrolling',1),('gColorFading',3)]:
 uc.mem_write(symbols[symbol],bytes([value]));call('SwitchWide_Prepare');assert call('SwitchWide_FrameHeight')==160;uc.mem_write(symbols[symbol],b'\0')
print('PASS: inactive/fade presentations restore native height',flush=True)
