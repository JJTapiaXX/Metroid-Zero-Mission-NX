"""Use real Chozodia room tiles for a stationary save-station foundation."""
from pathlib import Path
source=Path(__file__).with_name('run_game_smoke.py').read_text();exec(compile(source.split("try:\n    path=allocate(16)")[0],'helpers','exec'))
p=allocate(16);uc.mem_write(p,b'mzm.gba\0');assert call('GameSwitch_Load',p)
for name,value in [('gMainGameMode',4),('gSubGameMode1',2)]:uc.mem_write(symbols[name],struct.pack('<H',value))
for name in ('gDisableDrawingSamusAndScrolling','gColorFading'):uc.mem_write(symbols[name],b'\0')
uc.mem_write(symbols['gInGameCutscene']+3,b'\0');uc.mem_write(symbols['virtuappu_registers']+4,b'\x01');uc.mem_write(symbols['gCurrentArea'],b'\x06');uc.mem_write(symbols['gCurrentRoom'],b'\x04')
for room in (4,15,21,27,40):
 uc.mem_write(symbols['gCurrentRoom'],bytes([room]));uc.mem_write(symbols['gSpriteData'],bytes(24*64))
 call('RoomLoadEntry');call('RoomLoadTileset');call('RoomLoadBackgrounds');call('SpriteLoadSpriteset');call('SpriteLoadRoomSprites')
 for bg in range(3):
  uc.mem_write(symbols[f'gBg{bg}XPosition'],bytes(2));uc.mem_write(symbols[f'gBg{bg}YPosition'],struct.pack('<H',32*4));call('RoomSetInitialTilemap',bg)
 sprite=symbols['gSpriteData'];uc.mem_write(sprite,b'\x03\0');uc.mem_write(sprite+24,bytes(uc.mem_read(symbols['p_sSavePlatformChozodiaOam_Idle'],8)));uc.mem_write(sprite+37,b'\x02');uc.mem_write(symbols['gNextOamSlot'],b'\0');call('SwitchWorld_BeginFrame');call('SwitchHud_NoteEnd',0);call('SpriteDraw',sprite,0);call('ResetFreeOam');uc.mem_write(symbols['gOamMem'],bytes(uc.mem_read(symbols['gOamData'],1024)))
 io=bytearray(0x400);struct.pack_into('<H',io,0,0x1600)
 for bg in (1,2):struct.pack_into('<H',io,8+bg*2,0x4004|(bg*2<<8)|bg);struct.pack_into('<HH',io,16+bg*4,0,32)
 uc.mem_write(symbols['gIoMem'],bytes(io));oam=bytes(uc.mem_read(symbols['gOamMem'],1024));vram=bytes(uc.mem_read(symbols['gVram'],0x4000))
 def render(scale):
  call('SwitchOverlay_SetOptions',0,scale);call('SwitchWide_Prepare');call('SwitchWide_CenterBegin');call('virtuappu_render_frame');call('SwitchWide_CenterEnd')
  assert bytes(uc.mem_read(symbols['gOamMem'],1024))==oam
  assert bytes(uc.mem_read(symbols['gVram'],0x4000))==vram
  return bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],(320*192 if scale==4 else 288*160 if scale==3 else 240*160)*4))
 native=render(1);uc.mem_write(sprite,bytes(2));before=render(4);uc.mem_write(sprite,b'\x03\0');after=render(4)
 assert b''.join(after[((y+16)*320+40)*4:((y+16)*320+280)*4] for y in range(160))==native,'native viewport changed'
 def foundation(data):return b''.join(data[(y*320+168)*4:(y*320+216)*4] for y in range(176,192))
 assert foundation(after)!=foundation(before),'floor missing'
 # Match the adjacent panel's two outer caps and repeat only its interior.
 columns=(0,1,2,1,2,3)
 source_x=struct.unpack('<i',uc.mem_read(symbols['save_floor_source_x'],4))[0]*8
 source_y=struct.unpack('<i',uc.mem_read(symbols['save_floor_source_y'],4))[0]*16-32
 clip,w,h=struct.unpack('<QHH',uc.mem_read(symbols['gBgPointersAndDimensions']+48,12))
 col=source_x//16;clip_row=(source_y+32)//16
 assert struct.unpack('<HH',uc.mem_read(clip+(clip_row*w+col)*2,4))==(16,16),'selected sloped flooring'
 assert struct.unpack('<HH',uc.mem_read(clip+((clip_row-1)*w+col)*2,4))==(0,0),'selected buried flooring'
 reference=render(3)
 for row in range(16):
  expected=b''.join(reference[((source_y+row)*288+source_x+24+c*8)*4:((source_y+row)*288+source_x+32+c*8)*4] for c in columns)
  actual=after[((176+row)*320+168)*4:((176+row)*320+216)*4]
  assert actual==expected,('foundation panel cap/interior mismatch',row)
 
 # Disable OBJ and alter its graphic tiles/position. Floor must not respond.
 uc.mem_write(symbols['gIoMem'],struct.pack('<H',0x600));base=render(4)
 for y in (700,708):
  uc.mem_write(sprite+2,struct.pack('<H',y));uc.mem_write(symbols['gVram']+0x14000,bytes([y&255])*0x4000)
  assert foundation(render(4))==foundation(base),'floor moved with animated machine'
 from PIL import Image
 for name,data in [('before',before),('after',after)]:Image.frombytes('RGBA',(320,192),data).resize((1200,720),Image.Resampling.NEAREST).save(args.output/(str(room)+'-'+name+'.png'))
 print('PASS: room',room,'real Chozodia room flooring fills shaft, remains fixed through platform movement/graphics changes, and preserves native pixels, OAM and VRAM',flush=True)
