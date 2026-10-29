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
uc.mem_write(symbols['gMainGameMode'],struct.pack('<H',4));uc.mem_write(symbols['gSubGameMode1'],struct.pack('<H',2));uc.mem_write(symbols['gInGameCutscene']+3,b'\0');uc.mem_write(symbols['gDisableDrawingSamusAndScrolling'],b'\0');uc.mem_write(symbols['gColorFading'],b'\0')
uc.mem_write(symbols['gCurrentRoomEntry']+layout['bg0Prop'],b'\x40');uc.mem_write(symbols['gCurrentRoomEntry']+layout['effectY'],struct.pack('<H',96*4))
uc.mem_write(symbols['gBgPointersAndDimensions']+24,struct.pack('<H',64));uc.mem_write(symbols['gBg1XPosition'],struct.pack('<H',512*4));uc.mem_write(symbols['gBg1YPosition'],bytes(2))
uc.mem_write(symbols['gBgPltt'],bytes(512));uc.mem_write(symbols['gBgPltt']+30,struct.pack('<H',0x03ff));uc.mem_write(symbols['gOamMem'],struct.pack('<4H',0x200,0,0,0)*128)
vram=bytearray(0x18000);vram[32:64]=bytes([0xff])*32
for row in range(12,32):vram[0xf800+row*64:0xf800+(row+1)*64]=struct.pack('<H',1)*32
uc.mem_write(symbols['gVram'],bytes(vram));uc.mem_write(symbols['virtuappu_registers']+4,b'\x01');black=struct.pack('<I',0xff000000)
for name,control,blend,mosaic in [('direct',0x100,0,0),('window',0x2100,0,0),('alpha',0x100,0x2041,0),('mosaic',0x100,0,0x33)]:
 io=bytearray(0x400);struct.pack_into('<H',io,0,control);struct.pack_into('<H',io,8,(31<<8)|(0x40 if mosaic else 0));struct.pack_into('<H',io,0x40,240);struct.pack_into('<H',io,0x44,160);struct.pack_into('<H',io,0x48,0x3f);struct.pack_into('<H',io,0x4c,mosaic);struct.pack_into('<HH',io,0x50,blend,0x0808)
 uc.mem_write(symbols['gIoMem'],bytes(io));call('SwitchOverlay_SetOptions',0,4);call('SwitchWide_Prepare');call('SwitchWide_CenterBegin');call('virtuappu_render_frame');pixels=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],320*192*4));call('SwitchWide_CenterEnd')
 row=pixels[136*320*4:137*320*4]
 assert all(row[x*4:(x+1)*4]!=black for x in range(320)),('liquid missing from Wide margin',name)
 assert pixels[96*320*4:97*320*4]==black*320,('liquid surface moved',name)
 assert pixels[:16*320*4]==black*(16*320),('liquid wrapped into top reveal',name)
 (args.output/(name+'.rgba')).write_bytes(pixels)
# Small rooms must still suppress liquid beyond their boundaries.
uc.mem_write(symbols['gBgPointersAndDimensions']+24,struct.pack('<H',15));uc.mem_write(symbols['gBg1XPosition'],bytes(2));uc.mem_write(symbols['gIoMem']+0x14,bytes(2));call('SwitchWide_Prepare');call('SwitchWide_CenterBegin');call('virtuappu_render_frame');pixels=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],320*192*4));call('SwitchWide_CenterEnd')
row=pixels[136*320*4:137*320*4];assert row[:40*4]==black*40 and row[280*4:]==black*40,'liquid leaked outside small room'
print('PASS: liquid reaches both margins in direct/window/alpha/mosaic paths, keeps its surface height, and stops at room edges',flush=True)
