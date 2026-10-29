"""Evaluate synthetic achievements through real frontend cutscene frames."""
from pathlib import Path
source=Path(__file__).with_name('run_game_smoke.py').read_text().split("try:\n    path=allocate(16)")[0]
source=source.replace("'0xH00153e=64.3.'","'0xH000c70=7'").replace("'0xH00153e=128.2.'","'0xH000c70=8'")
exec(compile(source,'helpers','exec'))
assert args.ra_test,'requires --ra-test for the fake service'
p=allocate(16);uc.mem_write(p,b'mzm.gba\0');assert call('GameSwitch_Load',p)
for _ in range(8):call('Achievements_Update')
assert call('Achievements_Count')==3
fake_online=False
# Keep frontend presentation deterministic; the achievement runtime and native
# GBA-address translation remain real. All service requests use test doubles.
def poll():uc.mem_write(uc.reg_read(UC_ARM64_REG_X8),struct.pack('<HBBBBxxiiBxxx',0,0,0,0,0,0,0,0));return_value()
hook('PlatformSwitch_Poll',poll);hook('PlatformSwitch_Present',lambda:return_value(1));hook('CallbackCallVblank',lambda:return_value());hook('virtuappu_render_frame',lambda:return_value())
for mode in (0,0,4,7,8):
 uc.mem_write(symbols['gMainGameMode'],struct.pack('<H',mode));uc.mem_write(symbols['gSubGameMode1'],bytes(2));call('GameSwitch_VBlank')
assert call('Achievements_Unlocked',0),'escape-mode condition not evaluated'
assert call('Achievements_Unlocked',1),'credits-mode condition not evaluated'
assert call('Achievements_Pending')==2,'unlocks not queued offline'
print('PASS: real frontend checks escape and credits frames; real rcheevos triggers both mode conditions and queues both unlocks offline (synthetic definitions, fake service)',flush=True)
