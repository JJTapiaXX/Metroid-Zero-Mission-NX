"""The second map uses retail objective coordinates, sprite and activation rules."""
from pathlib import Path
from PIL import Image
source=Path(__file__).with_name('run_game_smoke.py').read_text();exec(compile(source.split("try:\n    path=allocate(16)")[0],'helpers','exec'))
p=allocate(16);uc.mem_write(p,b'mzm.gba\0');assert call('GameSwitch_Load',p)
uc.mem_write(symbols['gCurrentArea'],b'\x02');uc.mem_write(symbols['gEventsTriggered'],bytes(32));uc.mem_write(symbols['gEquipment'],bytes(20));uc.mem_write(symbols['gDecompressedMinimapVisitedTiles'],bytes(2048));uc.mem_write(symbols['gMinimapX'],b'\x11');uc.mem_write(symbols['gMinimapY'],b'\x08')
events=struct.unpack('<Q',uc.mem_read(symbols['p_sChozoStatueHintEvents'],8))[0];event=uc.mem_read(events+4,1)[0]
input_ptr=allocate(32);uc.mem_write(input_ptr,struct.pack('<HBBBBxxiiBxxx',0,0,1,0,0,0,0,0))
def render(name):
 image=bytes(uc.mem_read(call('SwitchUI_Render',input_ptr),320*240*4));Image.frombytes('RGBA',(320,240),image).save(args.output/(name+'.png'));return image
def yellow(image,x,y):
 count=0
 for yy in range(y-8,y+8):
  for xx in range(x-8,x+8):
   r,g,b,a=image[(yy*320+xx)*4:(yy*320+xx)*4+4]
   count+=r>170 and g>130 and b<140
 return count
pause_before=bytes(uc.mem_read(symbols['gNonGameplayRam'],0x1000));vram_before=bytes(uc.mem_read(symbols['gVram'],0x18000))
before=render('before');assert call('ChozoStatueHintCheckTargetIsActivated',4)&0xffffffff!=1
call('EventFunction',1,event);assert call('ChozoStatueHintCheckTargetIsActivated',4)==1
active=render('active');assert yellow(active,176,120)>yellow(before,176,120),'yellow objective missing at retail coordinates'
uc.mem_write(symbols['gEquipment']+14,b'\x01');assert call('ChozoStatueHintCheckTargetIsActivated',4)==0
completed=render('completed');assert yellow(completed,176,120)<yellow(active,176,120),'completed objective still displayed'
uc.mem_write(symbols['gEquipment']+14,b'\0')
# Zoom-out must reposition and scale the same marker.
uc.mem_write(input_ptr,struct.pack('<HBBBBxxiiBxxx',0,0,1,1,1,48,224,0));zoomed=render('zoomed');assert yellow(zoomed,168,120)>0,'objective missing at overview zoom'
assert bytes(uc.mem_read(symbols['gNonGameplayRam'],0x1000))==pause_before
assert bytes(uc.mem_read(symbols['gVram'],0x18000))==vram_before
print('PASS: retail High Jump hint appears at its map coordinates, disappears on acquisition, and follows overview zoom',flush=True)
