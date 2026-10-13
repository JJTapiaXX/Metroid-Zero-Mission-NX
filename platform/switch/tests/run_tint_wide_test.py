"""Preserve palette-based room tints across centered Wide margins."""
from pathlib import Path
source=Path(__file__).with_name('run_game_smoke.py').read_text();exec(compile(source.split("try:\n    path=allocate(16)")[0],'helpers','exec'))
p=allocate(16);uc.mem_write(p,b'mzm.gba\0');assert call('GameSwitch_Load',p)
for name,value in [('gMainGameMode',4),('gSubGameMode1',2)]:uc.mem_write(symbols[name],struct.pack('<H',value))
uc.mem_write(symbols['gInGameCutscene']+3,b'\0');uc.mem_write(symbols['gDisableDrawingSamusAndScrolling'],b'\0');uc.mem_write(symbols['gColorFading'],b'\0');uc.mem_write(symbols['virtuappu_registers']+4,b'\x01')
uc.mem_write(symbols['gBgPointersAndDimensions']+24,struct.pack('<H',64));uc.mem_write(symbols['gBg1XPosition'],struct.pack('<H',256*4));uc.mem_write(symbols['gOamMem'],struct.pack('<4H',0x200,0,0,0)*128);uc.mem_write(symbols['gVram'],bytes(0x18000))
black=struct.pack('<I',0xff000000)
for color in (0x001f,0x0410,0x101f):
 uc.mem_write(symbols['gBgPltt'],struct.pack('<H',color))
 for name,control,blend,mosaic in [('direct',0x100,0,0),('window',0x2100,0,0),('brighten',0x100,0xa0,0),('mosaic',0x100,0,0x33)]:
  io=bytearray(0x400);struct.pack_into('<H',io,0,control);struct.pack_into('<H',io,8,(31<<8)|(0x40 if mosaic else 0));struct.pack_into('<H',io,0x14,256);struct.pack_into('<H',io,0x40,240);struct.pack_into('<H',io,0x44,160);struct.pack_into('<H',io,0x48,0x3f);struct.pack_into('<H',io,0x4c,mosaic);struct.pack_into('<HHH',io,0x50,blend,0x0808,8)
  uc.mem_write(symbols['gIoMem'],bytes(io));call('SwitchOverlay_SetOptions',0,3);call('SwitchWide_Prepare');call('SwitchWide_CenterBegin');call('virtuappu_render_frame');pixels=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],288*160*4));call('SwitchWide_CenterEnd')
  for y in (0,80,159):
   row=pixels[y*288*4:(y+1)*288*4];reference=row[144*4:145*4];assert reference!=black
   assert row==reference*288,('tint clipped in Wide margin',color,name,y)
uc.mem_write(symbols['gBgPointersAndDimensions']+24,struct.pack('<H',15));uc.mem_write(symbols['gBg1XPosition'],bytes(2));uc.mem_write(symbols['gIoMem']+0x14,bytes(2));call('SwitchWide_Prepare');call('SwitchWide_CenterBegin');call('virtuappu_render_frame');pixels=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],288*160*4));call('SwitchWide_CenterEnd')
row=pixels[80*288*4:81*288*4];assert row[:24*4]==black*24 and row[264*4:]==black*24,'tint leaked outside room boundaries'
print('PASS: room backdrop tints match across both Wide margins in direct/window/brightness/mosaic paths; outside-room borders remain black',flush=True)
