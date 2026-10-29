"""Keep finite sky backgrounds from wrapping in the extra top rows."""
from pathlib import Path
source=Path(__file__).with_name('run_game_smoke.py').read_text()
exec(compile(source.split("try:\n    path=allocate(16)")[0],'helpers','exec'))

p=allocate(16);uc.mem_write(p,b'mzm.gba\0');assert call('GameSwitch_Load',p)
for name,value in [('gMainGameMode',4),('gSubGameMode1',2)]:uc.mem_write(symbols[name],struct.pack('<H',value))
for name in ('gDisableDrawingSamusAndScrolling','gColorFading'):uc.mem_write(symbols[name],b'\0')
uc.mem_write(symbols['gInGameCutscene']+3,b'\0');uc.mem_write(symbols['virtuappu_registers']+4,b'\x01');uc.mem_write(symbols['gOamMem'],struct.pack('<4H',0x200,0,0,0)*128)
uc.mem_write(symbols['gBgPointersAndDimensions']+24,struct.pack('<HH',64,64));uc.mem_write(symbols['gBg1XPosition'],struct.pack('<H',512*4));uc.mem_write(symbols['gBg1YPosition'],struct.pack('<H',256*4))
backdrop=struct.pack('<H',1)*(32*16)+struct.pack('<H',2)*(32*16)
def load():uc.mem_write(uc.reg_read(UC_ARM64_REG_X0),backdrop);return_value(len(backdrop))
hook('SwitchRoom_LoadBackdrop',load)
uc.mem_write(symbols['gVram']+0x8000+32,bytes([0x11])*32);uc.mem_write(symbols['gVram']+0x8000+64,bytes([0x22])*32);uc.mem_write(symbols['gVram']+0x3000,backdrop)
uc.mem_write(symbols['gBgPltt'],struct.pack('<3H',0,0x03ff,0x03e0)+bytes(506))
for control in (0x800,0x2800):
 io=bytearray(0x400);struct.pack_into('<H',io,0,control);struct.pack_into('<H',io,14,(6<<8)|8);struct.pack_into('<HH',io,0x14,0,256);struct.pack_into('<H',io,0x40,240);struct.pack_into('<H',io,0x44,160);struct.pack_into('<H',io,0x48,0x3f);uc.mem_write(symbols['gIoMem'],bytes(io))
 def render(scale):
  call('SwitchOverlay_SetOptions',0,scale);call('SwitchWide_Prepare');call('SwitchWide_CenterBegin');call('virtuappu_render_frame');call('SwitchWide_CenterEnd')
  return bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],(320*192 if scale==4 else 240*160)*4))
 native=render(1);zoom=render(4)
 assert zoom[:320*16*4]==native[:4]*(320*16),'bottom of sky wrapped into top reveal'
 crop=b''.join(zoom[((y+16)*320+40)*4:((y+16)*320+280)*4] for y in range(160));assert crop==native,'native backdrop changed'
print('PASS: sky top extends into Zoom Out without bottom wrapping; native viewport preserved in direct/window paths',flush=True)
