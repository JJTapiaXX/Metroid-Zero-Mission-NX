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

def members(name):
 for cu in dwarf.iter_CUs():
  for die in cu.iter_DIEs():
   if die.tag=='DW_TAG_structure_type' and die.attributes.get('DW_AT_name') and die.attributes['DW_AT_name'].value.decode()==name:
    return {m.attributes['DW_AT_name'].value.decode():m.attributes['DW_AT_data_member_location'].value for m in die.iter_children() if m.tag=='DW_TAG_member' and 'DW_AT_name' in m.attributes}
# Keep DWARF stream open for lazy type reads.
with open(args.elf,'rb') as stream:
 dwarf=ELFFile(stream).get_dwarf_info();physics=members('SamusPhysics');samus=members('SamusData')
for name,value in [('gMainGameMode',4),('gSubGameMode1',2)]:uc.mem_write(symbols[name],struct.pack('<H',value))
for name in ('gDisableDrawingSamusAndScrolling','gColorFading','gHideHud'):uc.mem_write(symbols[name],b'\0')
uc.mem_write(symbols['gInGameCutscene']+3,b'\0');uc.mem_write(symbols['virtuappu_registers']+4,b'\x01')
uc.mem_write(symbols['gBgPointersAndDimensions']+24,struct.pack('<HH',64,64));uc.mem_write(symbols['gBg1XPosition'],struct.pack('<H',512*4));uc.mem_write(symbols['gBg1YPosition'],struct.pack('<H',256*4))
uc.mem_write(symbols['gCurrentRoomEntry']+layout['bg0Prop'],b'\x40');uc.mem_write(symbols['gCurrentRoomEntry']+layout['effectY'],b'\xff\xff')
uc.mem_write(symbols['gOamMem'],struct.pack('<4H',0x200,0,0,0)*128)
uc.mem_write(symbols['gBgPltt'],bytes(512));uc.mem_write(symbols['gBgPltt']+30,struct.pack('<H',0x03ff));uc.mem_write(symbols['gVram']+32,bytes([0xff])*32);uc.mem_write(symbols['gVram']+0xf800,struct.pack('<H',1)*1024)
for scale,width,height in ((3,288,160),(4,320,192)):
 io=bytearray(0x400);struct.pack_into('<H',io,0,0x100);struct.pack_into('<H',io,8,31<<8);struct.pack_into('<HH',io,0x14,0,256);uc.mem_write(symbols['gIoMem'],bytes(io))
 call('SwitchOverlay_SetOptions',0,scale);call('SwitchWide_Prepare');call('SwitchWide_CenterBegin');call('virtuappu_render_frame');call('SwitchWide_CenterEnd')
 pixels=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],width*height*4));assert len(set(struct.iter_unpack('<I',pixels)))==1,('cloud layer clipped',scale)
print('PASS: non-liquid cloud BG0 covers Wide and all four Zoom Out margins',flush=True)
# Exercise actual SamusDraw rather than marking a synthetic sprite as world data.
# GameSwitch_Load leaves the untouched Samus state zero-initialized.
uc.mem_write(symbols['gSamusData']+samus['xPosition'],struct.pack('<H',512*4+100*4));uc.mem_write(symbols['gSamusData']+samus['yPosition'],struct.pack('<H',256*4))
body=allocate(16);uc.mem_write(body,struct.pack('<4H',1,248,0,0));uc.mem_write(symbols['gSamusPhysics']+physics['pBodyOam'],struct.pack('<Q',body));uc.mem_write(symbols['gNextOamSlot'],bytes(2))
call('SwitchHud_BeginFrame');call('SwitchHud_NoteEnd',0);call('Port_OverlayText_BeginFrame');call('SwitchWorld_BeginFrame');call('SamusDraw')
raw_y=uc.mem_read(symbols['gOamData'],1)[0];actual=call('SwitchWorld_PositionY',0,raw_y);assert actual==((raw_y-256)&0xffffffff),('Samus world Y missing',raw_y,actual)
uc.mem_write(symbols['gOamMem'],struct.pack('<4H',0x200,0,0,0)*128);uc.mem_write(symbols['gOamMem'],bytes(uc.mem_read(symbols['gOamData'],8)))
uc.mem_write(symbols['gVram']+0x10000,bytes([0xff])*32);uc.mem_write(symbols['gObjPltt']+30,struct.pack('<H',0x7fff))
io=bytearray(0x400);struct.pack_into('<H',io,0,0x1000);struct.pack_into('<HH',io,0x14,0,256);uc.mem_write(symbols['gIoMem'],bytes(io));call('SwitchOverlay_SetOptions',0,4);call('SwitchWide_Prepare');call('SwitchWide_CenterBegin');call('virtuappu_render_frame');call('SwitchWide_CenterEnd')
pixels=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],320*192*4));assert pixels[(10*320+140)*4:(10*320+141)*4]!=struct.pack('<I',0xff000000),'Samus clipped at native top'
print('PASS: real SamusDraw sprite remains visible above the native top edge',flush=True)
