"""Validate and draw every Ruins Test animation after native ROM conversion."""
from pathlib import Path
import re
source=Path(__file__).with_name('run_game_smoke.py').read_text()
exec(compile(source.split("try:\n    path=allocate(16)")[0],'helpers','exec'))
p=allocate(16);uc.mem_write(p,b'mzm.gba\0');assert call('GameSwitch_Load',p)
generated=Path(__file__).resolve().parents[3]/'port/generated/sprites_ruins_test_rom.c'
us=generated.read_text().split('case PORT_ROM_REGION_US: {')[1].split('break;')[0]
arrays=re.findall(r'(p_s\w+) = \(const struct FrameData\(\*\)\[(\d+)\]\)Port_ResolveRomData\(0x([0-9a-f]+)u\)',us)
assert len(arrays)>20,'Ruins Test animation declarations missing'
sprite=allocate(64);frames_checked=0
# Exercise the inlined animation updater through the real sprite dispatcher.
hook('RuinsTest',lambda:return_value())
ai=bytes(uc.mem_read(symbols['sPrimarySpritesAIPointers'],200*8))
sprite_id=ai.index(struct.pack('<Q',symbols['RuinsTest']))//8
uc.mem_write(symbols['gSubGameMode1'],struct.pack('<H',6))
for name,length,address in arrays:
 pointer=struct.unpack('<Q',uc.mem_read(symbols[name],8))[0];address=int(address,16)
 for index in range(int(length)):
  expected,timer=struct.unpack_from('<IB',rom,address-0x08000000+index*8)
  actual=struct.unpack('<Q',uc.mem_read(pointer+index*16,8))[0];actual_timer=uc.mem_read(pointer+index*16+8,1)[0]
  assert (actual,actual_timer)==(expected,timer),(name,index,'unconverted 32-bit animation',hex(actual),hex(expected),actual_timer,timer)
  if not timer:continue
  data=bytearray(64);struct.pack_into('<H',data,0,1);struct.pack_into('<HH',data,2,320,320);struct.pack_into('<H',data,22,index);struct.pack_into('<Q',data,24,pointer);uc.mem_write(sprite,bytes(data));uc.mem_write(symbols['gNextOamSlot'],b'\0')
  call('SwitchWorld_BeginFrame');call('SpriteDraw',sprite,0);frames_checked+=1
 print('PASS:',name,'frames drawn',flush=True)
 # Exercise the game's timer/loop code, not just pointer inspection.
 data=bytearray(64);struct.pack_into('<H',data,0,1);data[33]=sprite_id;struct.pack_into('<Q',data,24,pointer);uc.mem_write(symbols['gSpriteData'],bytes(data))
 for tick in range(60):
  call('SpriteUpdate')
  current=struct.unpack('<H',uc.mem_read(symbols['gSpriteData']+22,2))[0];assert current<int(length)-1,(name,'animation ran beyond sentinel',current)
print(f'PASS: {len(arrays)} Ruins Test animation tables converted, {frames_checked} frames drawn, and 60 update ticks per table',flush=True)
