"""Run the Chozo-ship escape cutscene through its final fade."""
from pathlib import Path
source=Path(__file__).with_name('run_game_smoke.py').read_text();exec(compile(source.split("try:\n    path=allocate(16)")[0],'helpers','exec'))
p=allocate(16);uc.mem_write(p,b'mzm.gba\0');assert call('GameSwitch_Load',p)
uc.mem_write(symbols['gMainGameMode'],struct.pack('<H',7));uc.mem_write(symbols['gSubGameMode1'],bytes(2))
for frame in range(10000):
 uc.mem_write(symbols['gFrameCounter8Bit'],bytes([frame&255]));uc.mem_write(symbols['gFrameCounter16Bit'],struct.pack('<H',frame&65535))
 ended=call('ChozodiaEscapeHandler')
 call('CallbackCallVblank')
 if frame%300==0: print('Escape frame',frame,'submode',struct.unpack('<H',uc.mem_read(symbols['gSubGameMode1'],2))[0],flush=True)
 if ended:
  print('PASS: full Chozo-ship escape, explosion, departure, mission-complete screen and final fade;',frame+1,'frames',flush=True)
  break
else:raise RuntimeError('Escape cutscene did not complete')
