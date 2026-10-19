"""Check multi-part particle effects across both Wide margins."""
from pathlib import Path
from elftools.elf.elffile import ELFFile
source=Path(__file__).with_name('run_game_smoke.py').read_text();exec(compile(source.split("try:\n    path=allocate(16)")[0],'helpers','exec'))
p=allocate(16);uc.mem_write(p,b'mzm.gba\0');assert call('GameSwitch_Load',p)
layouts={}
with open(args.elf,'rb') as stream:
 for cu in ELFFile(stream).get_dwarf_info().iter_CUs():
  for die in cu.iter_DIEs():
   name=die.attributes.get('DW_AT_name')
   if die.tag=='DW_TAG_structure_type' and name and name.value in (b'ParticleEffect',b'FrameData'):
    layouts[name.value.decode()]={m.attributes['DW_AT_name'].value.decode():m.attributes['DW_AT_data_member_location'].value for m in die.iter_children() if m.tag=='DW_TAG_member' and 'DW_AT_name' in m.attributes}
  if len(layouts)==2:break
uc.mem_write(symbols['gMainGameMode'],struct.pack('<H',4));uc.mem_write(symbols['gSubGameMode1'],struct.pack('<H',2));uc.mem_write(symbols['gColorFading'],b'\0');uc.mem_write(symbols['gInGameCutscene']+3,b'\0');uc.mem_write(symbols['gDisableDrawingSamusAndScrolling'],b'\0');uc.mem_write(symbols['gBgPointersAndDimensions']+24,struct.pack('<H',64));uc.mem_write(symbols['gBg1XPosition'],struct.pack('<H',512*4));uc.mem_write(symbols['gBg1YPosition'],bytes(2));uc.mem_write(symbols['virtuappu_registers']+4,b'\x01')
io=bytearray(0x400);struct.pack_into('<H',io,0,0x1000);uc.mem_write(symbols['gIoMem'],bytes(io));uc.mem_write(symbols['gBgPltt'],bytes(512));uc.mem_write(symbols['gObjPltt'],struct.pack('<256H',*([0x7fff]*256)));uc.mem_write(symbols['gVram']+0x10000,bytes([0xff])*0x8000)
frame=allocate(32)
uc.mem_write(frame,struct.pack('<7H',2,0,0,0,0,8,0))
uc.mem_write(symbols['gCurrentParticleEffectOamFramePointer'],struct.pack('<Q',frame))
particle=allocate(32)
def setfield(name,value,size):uc.mem_write(particle+layouts['ParticleEffect'][name],value.to_bytes(size,'little'))
black=struct.pack('<I',0xff000000)
for flipped in (False,True):
 for x in (-8,0,232,240,248):
  call('SwitchHud_BeginFrame');call('SwitchHud_NoteEnd',0);call('SwitchWorld_BeginFrame');call('Port_OverlayText_BeginFrame');uc.mem_write(symbols['gNextOamSlot'],b'\0')
  uc.mem_write(symbols['gOamData'],struct.pack('<4H',0x200,0,0,0)*128)
  setfield('status',3|(128 if flipped else 0),1);setfield('xPosition',(512+x)*4,2);setfield('yPosition',80*4,2)
  call('SwitchOverlay_SetOptions',0,3);call('ParticleCheckOnScreen' if 'ParticleCheckOnScreen' in symbols else 'ParticleCheckOnScreen.part.0',particle)
  assert uc.mem_read(particle+layouts['ParticleEffect']['status'],1)[0]&2,('particle despawned',x,flipped)
  call('ParticleDraw',particle);uc.mem_write(symbols['gOamMem'],bytes(uc.mem_read(symbols['gOamData'],1024)))
  call('SwitchWide_Prepare');call('SwitchWide_CenterBegin');call('virtuappu_render_frame');pixels=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],288*160*4));call('SwitchWide_CenterEnd')
  row=pixels[80*288*4:81*288*4];start=x-16 if flipped else x
  expected=set(range(max(0,start+24),min(288,start+40)))
  visible={i for i in range(288) if row[i*4:i*4+4]!=black}
  assert visible==expected,('particle effect clipped',x,flipped,sorted(visible),sorted(expected))
print('PASS: actual ParticleCheckOnScreen/ParticleDraw renders both parts across native and Wide edges, including flipped effects',flush=True)

# Absolute particles, including HUD-style effects, must not acquire a world origin.
call('SwitchWorld_BeginFrame');uc.mem_write(symbols['gNextOamSlot'],b'\0')
setfield('status',3|64,1);setfield('xPosition',120,2);setfield('yPosition',80,2)
call('ParticleDraw',particle)
assert call('SwitchWorld_Position',0,120)&0xffffffff==(-32768)&0xffffffff
assert call('SwitchWorld_Position',1,128)&0xffffffff==(-32768)&0xffffffff
print('PASS: absolute-position particle slots remain untagged and retain HUD positioning',flush=True)
