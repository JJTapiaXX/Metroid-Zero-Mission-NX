"""Check repeating liquid BG0 across both centered Wide margins."""
from pathlib import Path
from elftools.elf.elffile import ELFFile
source=Path(__file__).with_name('run_game_smoke.py').read_text();exec(compile(source.split("try:\n    path=allocate(16)")[0],'helpers','exec'))
p=allocate(16);uc.mem_write(p,b'mzm.gba\0');assert call('GameSwitch_Load',p)
with open(args.elf,'rb') as stream:
 dwarf=ELFFile(stream).get_dwarf_info();layout=None
 for cu in dwarf.iter_CUs():
  for die in cu.iter_DIEs():
   if die.tag=='DW_TAG_structure_type' and die.attributes.get('DW_AT_name') and die.attributes['DW_AT_name'].value==b'RoomEntry':
    layout={m.attributes['DW_AT_name'].value.decode():m.attributes['DW_AT_data_member_location'].value for m in die.iter_children() if m.tag=='DW_TAG_member' and 'DW_AT_name' in m.attributes};break
  if layout:break

for name,value in [('gMainGameMode',4),('gSubGameMode1',2)]:uc.mem_write(symbols[name],struct.pack('<H',value))
for name in ('gDisableDrawingSamusAndScrolling','gColorFading'):uc.mem_write(symbols[name],b'\0')
uc.mem_write(symbols['gInGameCutscene']+3,b'\0');uc.mem_write(symbols['virtuappu_registers']+4,b'\x01');uc.mem_write(symbols['gBgPointersAndDimensions']+24,struct.pack('<HH',64,64));uc.mem_write(symbols['gBg1XPosition'],struct.pack('<H',512*4));uc.mem_write(symbols['gBg1YPosition'],struct.pack('<H',256*4));uc.mem_write(symbols['gCurrentRoomEntry']+layout['bg0Prop'],b'\x40');uc.mem_write(symbols['gCurrentRoomEntry']+layout['effectY'],b'\xff\xff')
uc.mem_write(symbols['gOamMem'],struct.pack('<4H',0x200,0,0,0)*128);uc.mem_write(symbols['gVram']+0x4000+32,bytes([0x11])*32);uc.mem_write(symbols['gVram']+0x1000,struct.pack('<H',1)*1024)
for direction,haze in [('expanding',7),('retracting',8)]:
 call('HazeSetupCode',haze)
 for x,y,radius in [(120,80,4),(120,80,40),(120,80,159),(0,0,60),(239,159,60)]:
  uc.mem_write(symbols['gCurrentPowerBomb']+2,bytes([radius]));uc.mem_write(symbols['gCurrentPowerBomb']+4,struct.pack('<HH',(512+x)*4,(256+y)*4));call('Haze_PowerBombExpanding' if haze==7 else 'Haze_PowerBombRetracting')
  uc.mem_write(symbols['gBgPltt'],bytes(512));uc.mem_write(symbols['gBgPltt']+2,struct.pack('<H',0x7fff))
  io=bytearray(0x400);struct.pack_into('<H',io,0,0x4100);struct.pack_into('<H',io,8,0x0204);struct.pack_into('<HH',io,0x14,0,256);struct.pack_into('<H',io,0x46,160);struct.pack_into('<HH',io,0x48,0x1f00,0x3f);struct.pack_into('<HHH',io,0x50,0xc1,0,12);uc.mem_write(symbols['gIoMem'],bytes(io))
  def render(scale):
   call('SwitchOverlay_SetOptions',0,scale);call('SwitchWide_Prepare');call('SwitchWide_CenterBegin');call('virtuappu_render_frame');call('SwitchWide_CenterEnd');width,height=(320,192) if scale==4 else (288,160) if scale==3 else (240,160)
   return bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],width*height*4))
  native=render(1)
  assert len(set(struct.iter_unpack('<I',native)))>1 or radius==159,'explosion window missing'
  for scale,width,height,shift,vertical in [(3,288,160,24,0),(4,320,192,40,16)]:
   pixels=render(scale);crop=b''.join(pixels[((row+vertical)*width+shift)*4:((row+vertical)*width+shift+240)*4] for row in range(160));assert crop==native,('native explosion changed',direction,x,y,radius,scale)
   # Check every added pixel against the original ellipse LUT, without clamping its bounds to the GBA screen.
   table=struct.unpack('<Q',uc.mem_read(symbols['p_sHaze_PowerBomb_WindowValuesPointers'],8))[0];gba=struct.unpack('<Q',uc.mem_read(table+radius*8,8))[0];shape=call('port_resolve_addr',gba)
   for row in range(height):
    line=row-vertical;slice=line-y+radius;left=right=0
    if 0<=slice<radius*2+1:
     r,l=struct.unpack('<hh',uc.mem_read(shape+slice*4,4));left=x+l*2;right=x+r*2
    for col in range(width):
     color=struct.unpack_from('<I',pixels,(row*width+col)*4)[0];inside=left<=col-shift<right
     assert color==(0xfff8f8f8 if inside else 0xff404040),('ellipse mismatch',direction,scale,x,y,radius,col,row,hex(color))
   if x==120 and radius==40:
    from PIL import Image
    Image.frombytes('RGBA',(width,height),pixels).resize((width*3,height*3),Image.Resampling.NEAREST).save(args.output/f'{direction}-{scale}.png')
 print('PASS:',direction,'native/Wide/Zoom Out shapes, all reveal margins and corner-centered explosions',flush=True)
