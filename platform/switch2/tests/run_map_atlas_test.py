#!/usr/bin/env python3
"""Exercise area/world/collectibles views with real ROM map graphics."""
from engine_test_helpers import bootstrap
bootstrap(globals())
from PIL import Image
p=allocate(16);uc.mem_write(p,b'mzm.gba\0');assert call('GameSwitch_Load',p)
# Populate only native RAM test doubles, never the user's ROM or save.
uc.mem_write(symbols['gItemsCollected'],bytes([255])*(8*64*4))
uc.mem_write(symbols['gItemsCollected'],bytes([1,1,0,0,2,2,0,0]))
uc.mem_write(symbols['gVisitedMinimapTiles'],bytes([255])*(8*32*4))
uc.mem_write(symbols['gEquipment'],struct.pack('<HHBBHH10B',999,250,30,30,999,250,30,30,255,255,255,255,127,0,0,0))
uc.mem_write(symbols['gCurrentArea'],b'\0');uc.mem_write(symbols['gMinimapX'],b'\x0e');uc.mem_write(symbols['gMinimapY'],b'\x0f')
call('PauseScreenGetMinimapData',0,symbols['gDecompressedMinimapVisitedTiles'])
call('MinimapSetDownloadedTiles',0,symbols['gDecompressedMinimapVisitedTiles'])
input_pointer=allocate(32)
def frame(x=0,y=0,held=False,pressed=False,released=False):
    uc.mem_write(input_pointer,struct.pack('<HBBBBxxiiBbxx',0,0,1,held,pressed,x,y,released,0))
    return call('SwitchUI_Render',input_pointer)
def tap(x,y):
    result=frame(x,y,True,True);frame(x,y,released=True);return result
def snapshot(name):
    pointer=frame();data=bytes(uc.mem_read(pointer,320*240*4))
    Image.frombytes('RGBA',(320,240),data).save(args.output/(name+'.png'))
    return data
vram=bytes(uc.mem_read(symbols['gVram'],0x18000));oam=bytes(uc.mem_read(symbols['gOamMem'],0x400))
tap(32,8);tap(50,224);area_image=snapshot('area-overview')
tap(160,34)
assert uc.mem_read(symbols['world_map'],1)==b'\1','World toggle failed'
assert struct.unpack('<i',uc.mem_read(symbols['zoom'],4))[0]==2,'World fit scale failed'
world_image=snapshot('connected-world')
assert world_image!=area_image,'World view did not change image'
for area_id in range(7):assert uc.mem_read(symbols['world_visible']+area_id,1)==b'\1'
tap(263,224);assert struct.unpack('<i',uc.mem_read(symbols['zoom'],4))[0]==4
frame(150,100,True,True);frame(120,120,True);frame(120,120,released=True)
snapshot('world-zoom-pan');tap(60,34);snapshot('world-fit')
tap(260,34);assert uc.mem_read(symbols['collection_modal'],1)==b'\1'
snapshot('collectibles');tap(220,224);assert uc.mem_read(symbols['hide_item_totals'],1)==b'\1'
tap(30,103);assert struct.unpack('<i',uc.mem_read(symbols['area'],4))[0]==1
assert uc.mem_read(symbols['follow'],1)==b'\0','Item-area selection was overwritten by Follow'
snapshot('collectibles-hidden');tap(60,224)
tap(96,8);snapshot('status');tap(225,224)
assert uc.mem_read(symbols['collection_modal'],1)==b'\1','Status area-map entry failed'
tap(60,224);tap(32,8);tap(160,34)
assert uc.mem_read(symbols['world_map'],1)==b'\0','Return to area view failed'
tap(160,34)
uc.mem_write(symbols['gVisitedMinimapTiles'],bytes(8*32*4));uc.mem_write(symbols['gEquipment']+16,b'\0')
snapshot('world-unexplored')
assert bytes(uc.mem_read(symbols['world_visible'],7))==bytes(7),'Unexplored area leaked into the world atlas'
assert bytes(uc.mem_read(symbols['gVram'],0x18000))==vram,'UI changed gameplay VRAM'
assert bytes(uc.mem_read(symbols['gOamMem'],0x400))==oam,'UI changed gameplay OAM'
print('PASS: native area/world maps, seven areas, zoom/pan/fit, item/spoiler/modal touch controls, status entry, and gameplay VRAM/OAM isolation.',flush=True)
