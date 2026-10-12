#!/usr/bin/env python3
"""Render synthetic PPU scenes against fixed reference hashes. Uses smoke ELF loader.
Pass ELF, ROM and --output as in run_game_smoke.py. No ROM or save is modified.
"""
from pathlib import Path
source=Path(__file__).with_name('run_game_smoke.py').read_text()
exec(compile(source.split("try:\n    path=allocate(16)")[0],'smoke_helpers','exec'))
import hashlib
path=allocate(16); uc.mem_write(path,b'mzm.gba\0')
assert call('GameSwitch_Load',path)
io=bytearray(0x400); struct.pack_into('<H',io,0,0x1100); struct.pack_into('<H',io,8,31<<8)
uc.mem_write(symbols['gIoMem'],bytes(io))
vram=bytearray(0x18000)
vram[32:64]=bytes([0x21,0x43,0x65,0x87]*8)
vram[0xf800:0x10000]=struct.pack('<H',1)*1024
vram[0x10000:0x10020]=bytes([0x21,0x43,0x65,0x87]*8)
uc.mem_write(symbols['gVram'],bytes(vram))
palette=struct.pack('<256H',*((i%32)|((i*3%32)<<5)|((i*7%32)<<10) for i in range(256)))
uc.mem_write(symbols['gBgPltt'],palette); uc.mem_write(symbols['gObjPltt'],palette)
oam=bytearray(struct.pack('<4H',0x200,0,0,0)*128)
for i in range(12): struct.pack_into('<4H',oam,i*8,i*11,i*19,0,0)
uc.mem_write(symbols['gOamMem'],bytes(oam))
for name,blend in [('normal',0),('brighten',0x81),('alpha',0x2150),('mosaic',0)]:
    if name=='mosaic':
        uc.mem_write(symbols['gIoMem']+0x4c,struct.pack('<H',0x3300))
        for i in range(12): struct.pack_into('<H',oam,i*8,i*11|0x1000)
        uc.mem_write(symbols['gOamMem'],bytes(oam))
    uc.mem_write(symbols['gIoMem']+0x50,struct.pack('<HHH',blend,0x0808,8))
    call('virtuappu_mode1_render_frame',symbols['virtuappu_registers'])
    pixels=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],240*160*4))
    (args.output/(name+'.rgba')).write_bytes(pixels)
    expected={
        'normal':'6a350470ff6cb0bcb9311b03e13cd53fdcdf796eac4b7e7f5241fabf7755aa2c',
        'brighten':'7ffa7f237b9af9dfbecda1724d26a047a746836031c96b24e33d12a0124b51e0',
        'alpha':'3121f336de3be69ed142e5bf1bccc8794d5ca65f74392262a87dfa273b32170f',
        'mosaic':'c300a743a8a3b6e0bdafbe3acdf422c6ff25398ed89c309b1a91e1149f77ac21'}
    assert hashlib.sha256(pixels).hexdigest()==expected[name],name
    print('PASS:',name)

# A parked OBJ at x=256 was wrongly visible inside the widened viewport.
uc.mem_write(symbols['virtuappu_registers'],struct.pack('<HHBB',288,288,1,0))
io=bytearray(0x400);struct.pack_into('<H',io,0,0x100);struct.pack_into('<H',io,8,0x4000|(30<<8))
uc.mem_write(symbols['gIoMem'],bytes(io))
vram=bytearray(0x18000);vram[32:64]=bytes([0x21,0x43,0x65,0x87]*8);vram[0xf000:0x10000]=struct.pack('<H',1)*2048
vram[0x10000:0x10020]=bytes([0xff]*32);uc.mem_write(symbols['gVram'],bytes(vram))
oam=bytearray(struct.pack('<4H',0x200,0,0,0)*128);struct.pack_into('<4H',oam,0,40,256,0,0);uc.mem_write(symbols['gOamMem'],bytes(oam))
call('virtuappu_mode1_render_frame',symbols['virtuappu_registers']);background=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],288*160*4))
uc.mem_write(symbols['gIoMem'],struct.pack('<H',0x1100));call('virtuappu_mode1_render_frame',symbols['virtuappu_registers'])
parked=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],288*160*4))
assert parked==background,'parked OBJ leaked into Wide columns'
struct.pack_into('<H',oam,2,232);uc.mem_write(symbols['gOamMem'],bytes(oam));call('virtuappu_mode1_render_frame',symbols['virtuappu_registers'])
edge=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],288*160*4))
assert edge!=background,'native edge sprite disappeared'
for y in range(160):assert edge[(y*288+240)*4:(y+1)*288*4]==background[(y*288+240)*4:(y+1)*288*4],'sprite leaked across original OBJ boundary'
print('PASS: parked and edge sprites retain retail OBJ visibility in Wide',flush=True)

# Known UI banners are translated by half the 48-pixel expansion.
call('Port_OverlayText_BeginFrame');call('Port_OverlayText_NoteBannerOam',0,1)
struct.pack_into('<H',oam,2,100);uc.mem_write(symbols['gOamMem'],bytes(oam));call('virtuappu_mode1_render_frame',symbols['virtuappu_registers'])
centered=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],288*160*4))
assert centered[(40*288+100)*4:(40*288+101)*4]==background[(40*288+100)*4:(40*288+101)*4],'banner remained at its original position'
assert centered[(40*288+124)*4:(40*288+125)*4]!=background[(40*288+124)*4:(40*288+125)*4],'banner was not centered'
call('Port_OverlayText_BeginFrame')

# The minimap is a sprite, never a BG0 background tile.
call('Port_OverlayText_BeginFrame');call('SwitchHud_BeginFrame');call('SwitchHud_NoteMinimap',0)
struct.pack_into('<H',oam,2,210);uc.mem_write(symbols['gOamMem'],bytes(oam));call('virtuappu_mode1_render_frame',symbols['virtuappu_registers'])
minimap=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],288*160*4))
assert minimap[(40*288+210)*4:(40*288+211)*4]==background[(40*288+210)*4:(40*288+211)*4],'minimap copy remained in old location'
assert minimap[(40*288+258)*4:(40*288+259)*4]!=background[(40*288+258)*4:(40*288+259)*4],'minimap sprite did not move right'
print('PASS: message/save centering and actual minimap sprite anchoring',flush=True)

# Empty room space must be transparent, not tile zero (which may contain graphics).
call('SwitchHud_BeginFrame');call('Port_OverlayText_BeginFrame')
uc.mem_write(symbols['gOamMem'],struct.pack('<4H',0x200,0,0,0)*128)
io=bytearray(0x400);struct.pack_into('<H',io,8,30<<8)
vram[0:32]=bytes([0xff]*32);vram[64:96]=bytes([0xff]*32);uc.mem_write(symbols['gVram'],bytes(vram))
shadow=allocate(32*10*2);validity=allocate(32*10)
for row in range(32):uc.mem_write(shadow+row*20,struct.pack('<H',2));uc.mem_write(validity+row*10,b'\x01')
uc.mem_write(symbols['virtuappu_mode1_ws_shadow'],struct.pack('<Q',shadow));uc.mem_write(symbols['virtuappu_mode1_ws_shadow_valid'],struct.pack('<Q',validity));uc.mem_write(symbols['virtuappu_mode1_ws_shadow_base_tile'],struct.pack('<I',30))
for name,control,blend,mosaic in [('direct',0x100,0,0),('window',0x2100,0,0),('mosaic',0x100,0x2041,3)]:
    struct.pack_into('<H',io,0,control);struct.pack_into('<H',io,8,(30<<8)|(0x40 if mosaic else 0));struct.pack_into('<H',io,0x40,240);struct.pack_into('<H',io,0x44,160);struct.pack_into('<H',io,0x48,0x3f);struct.pack_into('<H',io,0x4c,mosaic);struct.pack_into('<HH',io,0x50,blend,0x0808)
    uc.mem_write(symbols['gIoMem'],bytes(io));call('virtuappu_mode1_render_frame',symbols['virtuappu_registers'])
    empty=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],288*160*4))
    assert empty[240*4:241*4]!=struct.pack('<I',0xff000000),name+' lost valid reveal tile'
    assert empty[248*4:249*4]==struct.pack('<I',0xff000000),name+' drew tile zero outside room'
print('PASS: absent room tiles stay transparent in direct, window and mosaic paths',flush=True)

# HUD pixels must survive when the revealed columns have no room background.
uc.mem_write(symbols['virtuappu_mode1_ws_reveal_mask'],struct.pack('<I',0))
uc.mem_write(symbols['virtuappu_mode1_ws_shadow'],bytes(32));uc.mem_write(symbols['virtuappu_mode1_ws_shadow_valid'],bytes(32))
call('SwitchHud_BeginFrame');call('SwitchHud_NoteMinimap',0)
oam=bytearray(struct.pack('<4H',0x200,0,0,0)*128);struct.pack_into('<4H',oam,0,0,210|(2<<14),0,0);uc.mem_write(symbols['gOamMem'],bytes(oam))
vram[0x10000:0x11000]=bytes([0xff]*4096);uc.mem_write(symbols['gVram'],bytes(vram))
for name,control,blend in [('direct',0x1100,0),('window',0x3100,0),('alpha',0x1100,0x2041)]:
    io=bytearray(0x400);struct.pack_into('<H',io,0,control);struct.pack_into('<H',io,8,30<<8)
    struct.pack_into('<H',io,0x40,240);struct.pack_into('<H',io,0x44,160);struct.pack_into('<H',io,0x48,0x3f)
    struct.pack_into('<HH',io,0x50,blend,0x0808);uc.mem_write(symbols['gIoMem'],bytes(io))
    call('virtuappu_mode1_render_frame',symbols['virtuappu_registers'])
    no_world=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],288*160*4))
    assert no_world[258*4:259*4]!=struct.pack('<I',0xff000000),name+' hid minimap over empty world'
    assert no_world[287*4:288*4]!=struct.pack('<I',0xff000000),name+' cut off minimap right edge'
    assert no_world[248*4:249*4]==struct.pack('<I',0xff000000),name+' exposed invalid background'
print('PASS: full minimap survives empty reveal columns in direct/window/alpha paths',flush=True)
