"""Run post-credits Samus portraits, ending text and record text."""
from pathlib import Path
source=Path(__file__).with_name('run_game_smoke.py').read_text();exec(compile(source.split("try:\n    path=allocate(16)")[0],'helpers','exec'))
p=allocate(16);uc.mem_write(p,b'mzm.gba\0');assert call('GameSwitch_Load',p)
uc.mem_write(symbols['gMainGameMode'],struct.pack('<H',8));uc.mem_write(symbols['gSubGameMode1'],struct.pack('<H',8));uc.mem_write(symbols['gEndingFlags'],b'\x01')
for frame in range(4000):
 uc.mem_write(symbols['gFrameCounter8Bit'],bytes([frame&255]));uc.mem_write(symbols['gFrameCounter16Bit'],struct.pack('<H',frame&65535));uc.mem_write(symbols['gChangedInput'],struct.pack('<H',1))
 ended=call('CreditsHandler');call('CallbackCallVblank')
 if frame%300==0:print('Ending portrait frame',frame,'submode',struct.unpack('<H',uc.mem_read(symbols['gSubGameMode1'],2))[0],flush=True)
 if ended:print('PASS: full post-credits portrait/results sequence with new-record banner;',frame+1,'frames',flush=True);break
else:raise RuntimeError('Ending portrait/results sequence did not finish')
