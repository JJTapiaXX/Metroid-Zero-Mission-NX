"""Exercise actual projectile OAM emission through both Wide margins."""
from pathlib import Path
from elftools.elf.elffile import ELFFile
source=Path(__file__).with_name('run_game_smoke.py').read_text();exec(compile(source.split("try:\n    path=allocate(16)")[0],'helpers','exec'))
p=allocate(16);uc.mem_write(p,b'mzm.gba\0');assert call('GameSwitch_Load',p)
layouts={}
with open(args.elf,'rb') as stream:
 for cu in ELFFile(stream).get_dwarf_info().iter_CUs():
  for die in cu.iter_DIEs():
   name=die.attributes.get('DW_AT_name')
   if die.tag=='DW_TAG_structure_type' and name and name.value in (b'ProjectileData',b'FrameData'):
    layouts[name.value.decode()]={m.attributes['DW_AT_name'].value.decode():m.attributes['DW_AT_data_member_location'].value for m in die.iter_children() if m.tag=='DW_TAG_member' and 'DW_AT_name' in m.attributes}
  if len(layouts)==2:break
uc.mem_write(symbols['gMainGameMode'],struct.pack('<H',4));uc.mem_write(symbols['gSubGameMode1'],struct.pack('<H',2));uc.mem_write(symbols['gColorFading'],b'\0');uc.mem_write(symbols['gInGameCutscene']+3,b'\0');uc.mem_write(symbols['gDisableDrawingSamusAndScrolling'],b'\0');uc.mem_write(symbols['gBgPointersAndDimensions']+24,struct.pack('<H',64));uc.mem_write(symbols['gBg1XPosition'],struct.pack('<H',512*4));uc.mem_write(symbols['gBg1YPosition'],bytes(2));uc.mem_write(symbols['virtuappu_registers']+4,b'\x01')
io=bytearray(0x400);struct.pack_into('<H',io,0,0x1000);uc.mem_write(symbols['gIoMem'],bytes(io));uc.mem_write(symbols['gBgPltt'],bytes(512));uc.mem_write(symbols['gObjPltt'],struct.pack('<256H',*([0x7fff]*256)));uc.mem_write(symbols['gVram']+0x10000,bytes([0xff])*0x8000)
part=allocate(16);uc.mem_write(part,struct.pack('<5H',1,0,0,0,0));frame=allocate(16);uc.mem_write(frame+layouts['FrameData']['pFrame'],struct.pack('<Q',part));proj=allocate(64)
def setfield(name,value,size):uc.mem_write(proj+layouts['ProjectileData'][name],value.to_bytes(size,'little'))
setfield('pOam',frame,8);setfield('drawDistanceOffset',64,1);setfield('yPosition',80*4,2)
black=struct.pack('<I',0xff000000)
for x in (-20,-8,232,240,244,260,272):
 call('SwitchHud_BeginFrame');call('SwitchHud_NoteEnd',0);call('SwitchWorld_BeginFrame');call('Port_OverlayText_BeginFrame');uc.mem_write(symbols['gNextOamSlot'],b'\0')
 uc.mem_write(symbols['gOamData'],struct.pack('<4H',0x200,0,0,0)*128);setfield('status',3,1);setfield('xPosition',(512+x)*4,2)
 call('SwitchOverlay_SetOptions',0,3);call('ProjectileCheckDespawn',proj)
 if x<=260:assert uc.mem_read(proj+layouts['ProjectileData']['status'],1)[0]&2,('beam despawned inside Wide',x)
 call('ProjectileDraw',proj);uc.mem_write(symbols['gOamMem'],bytes(uc.mem_read(symbols['gOamData'],1024)))
 call('SwitchWide_Prepare');call('SwitchWide_CenterBegin');call('virtuappu_render_frame');pixels=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],288*160*4));call('SwitchWide_CenterEnd')
 row=pixels[80*288*4:81*288*4]
 if x<=260:assert row[(x+24)*4:(x+25)*4]!=black,('beam clipped inside Wide',x)
 else:assert row==black*288,'offscreen beam wrapped into view'
print('PASS: actual projectile draw/despawn keeps beams visible in both margins and hides positions beyond Wide',flush=True)
