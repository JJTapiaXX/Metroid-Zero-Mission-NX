"""Exercise actual file-menu erase text without deleting a file."""
from pathlib import Path
from elftools.elf.elffile import ELFFile
source=Path(__file__).with_name('run_game_smoke.py').read_text();exec(compile(source.split("try:\n    path=allocate(16)")[0],'helpers','exec'))
p=allocate(16);uc.mem_write(p,b'mzm.gba\0');assert call('GameSwitch_Load',p)
uc.mem_write(symbols['gSubGameMode1'],bytes(2));call('FileSelectMenuHandler')
with open(args.elf,'rb') as stream:
 dwarf=ELFFile(stream).get_dwarf_info();layout=None
 for cu in dwarf.iter_CUs():
  for die in cu.iter_DIEs():
   if die.tag=='DW_TAG_structure_type' and die.attributes.get('DW_AT_name') and die.attributes['DW_AT_name'].value==b'FileSelectData':
    layout={m.attributes['DW_AT_name'].value.decode():m.attributes['DW_AT_data_member_location'].value for m in die.iter_children() if m.tag=='DW_TAG_member' and 'DW_AT_name' in m.attributes};break
  if layout:break
base=struct.unpack('<Q',uc.mem_read(symbols['sNonGameplayRamPointer'],8))[0]
uc.mem_write(base+layout['messageInfoIdQueue'],bytes([6,255,255]));uc.mem_write(base+layout['processTextStage'],b'\x01')
for i in range(6):
 uc.mem_write(symbols['gSubGameMode1'],struct.pack('<H',2));call('FileSelectMenuHandler')
upper=bytes(uc.mem_read(symbols['gVram']+0x6000,0x400));lower=bytes(uc.mem_read(symbols['gVram']+0x6400,0x400))
assert any(upper) and any(lower),'confirmation choices were drawn onto zero-filled glyph buffers'
from PIL import Image
im=Image.new('RGB',(256,16),'#00334d')
for y in range(16):
 for x in range(256):
  data=upper if y<8 else lower;v=data[(x//8)*32+(y%8)*4+(x%8)//2];c=(v>>(4*(x&1)))&15
  if c not in (0,15):im.putpixel((x,y),(240,240,240) if c==4 else (50,50,60))
im.resize((1024,64)).save(args.output/'erase-choices.png')
assert any(v not in (0,255) for v in upper),'choices contain no glyph strokes'
print('PASS: actual US erase confirmation produces Yes/No glyphs on initialized text buffers',flush=True)
