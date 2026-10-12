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
