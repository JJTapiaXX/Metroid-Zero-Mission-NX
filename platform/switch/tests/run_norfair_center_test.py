"""Load actual Norfair room data and compare the untouched native viewport."""
from pathlib import Path
source=Path(__file__).with_name('run_game_smoke.py').read_text()
exec(compile(source.split("try:\n    path=allocate(16)")[0],'smoke_helpers','exec'))
path=allocate(16);uc.mem_write(path,b'mzm.gba\0');assert call('GameSwitch_Load',path)
for name,value,size in [('gCurrentArea',2,1),('gMainGameMode',4,2),('gSubGameMode1',2,2)]:uc.mem_write(symbols[name],value.to_bytes(size,'little'))
uc.mem_write(symbols['gInGameCutscene']+3,b'\0');uc.mem_write(symbols['gDisableDrawingSamusAndScrolling'],b'\0');uc.mem_write(symbols['gColorFading'],b'\0')
uc.mem_write(symbols['gOamMem'],struct.pack('<4H',0x200,0,0,0)*128)
for room in range(8):
    uc.mem_write(symbols['gCurrentRoom'],bytes([room]));call('RoomLoadEntry');call('RoomLoadTileset');call('RoomLoadBackgrounds')
    dims=[struct.unpack('<QHH',uc.mem_read(symbols['gBgPointersAndDimensions']+i*16,12)) for i in range(3)]
    room_width=dims[1][1]*16
    for camera in (0,max(0,room_width-240)):
        for bg in range(3):
            uc.mem_write(symbols[f'gBg{bg}XPosition'],struct.pack('<H',camera*4));uc.mem_write(symbols[f'gBg{bg}YPosition'],b'\0\0');call('RoomSetInitialTilemap',bg)
        # The engine's room-map screenblocks are BG0=0, BG1=2, BG2=4; graphics use charbase 1.
        io=bytearray(0x400);struct.pack_into('<H',io,0,0x700)
        for bg in range(3):struct.pack_into('<H',io,8+bg*2,0x4004|(bg*2<<8)|bg);struct.pack_into('<HH',io,16+bg*4,camera&511,0)
        uc.mem_write(symbols['gIoMem'],bytes(io));uc.mem_write(symbols['virtuappu_registers']+4,b'\x01')
        call('SwitchOverlay_SetOptions',0,1);call('SwitchWide_Prepare');call('virtuappu_render_frame')
        native=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],240*160*4))
        call('SwitchOverlay_SetOptions',0,3);call('SwitchWide_Prepare');call('SwitchWide_CenterBegin');call('virtuappu_render_frame');call('SwitchWide_CenterEnd')
        wide=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],288*160*4))
        preserved=b''.join(wide[(y*288+24)*4:(y*288+264)*4] for y in range(160))
        assert preserved==native,f'Norfair room {room}, camera {camera}: native background changed'
        if camera>=24:
            for bg in range(3):
                uc.mem_write(symbols[f'gBg{bg}XPosition'],struct.pack('<H',(camera-24)*4));call('RoomSetInitialTilemap',bg)
                uc.mem_write(symbols['gIoMem']+16+bg*4,struct.pack('<H',(camera-24)&511))
            call('SwitchOverlay_SetOptions',0,1);call('SwitchWide_Prepare');call('virtuappu_render_frame')
            left_reference=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],240*160*4))
            for y in range(160):assert wide[y*288*4:(y*288+24)*4]==left_reference[y*240*4:(y*240+24)*4],('left reveal mismatch',room,camera,y)
        (args.output/f'norfair-{room}-{camera}.rgba').write_bytes(wide)
    print('PASS: Norfair room',room,'native pixels and edge camera',flush=True)



# World/Samus sprites shift with the view; HUD and minimap remain at screen edges.
uc.mem_write(symbols['gBgPointersAndDimensions']+24,struct.pack('<H',64));uc.mem_write(symbols['gBg1XPosition'],struct.pack('<H',256*4))
io=bytearray(0x400);struct.pack_into('<H',io,0,0x1000);struct.pack_into('<H',io,0x14,256);uc.mem_write(symbols['gIoMem'],bytes(io))
uc.mem_write(symbols['gBgPltt'],bytes(512));uc.mem_write(symbols['gObjPltt']+30,struct.pack('<H',0x7fff));uc.mem_write(symbols['gVram']+0x10000,bytes([0xff])*32)
call('SwitchHud_BeginFrame');call('SwitchHud_NoteEnd',1);call('SwitchHud_NoteMinimap',1);call('Port_OverlayText_BeginFrame');call('SwitchWorld_BeginFrame');call('SwitchWorld_NoteSlots',3,4,-8);call('SwitchWorld_NoteSlots',5,6,244)
oam=bytearray(struct.pack('<4H',0x200,0,0,0)*128)
for slot,y,x in [(0,0,4),(1,0,210),(2,40,100),(3,80,504),(4,120,256),(5,100,244)]:struct.pack_into('<4H',oam,slot*8,y,x,0,0)
uc.mem_write(symbols['gOamMem'],bytes(oam));call('SwitchOverlay_SetOptions',0,3);call('SwitchWide_Prepare')
before_map=bytes(uc.mem_read(symbols['gVram'],0x4000));before_io=bytes(uc.mem_read(symbols['gIoMem']+0x10,16))
call('SwitchWide_CenterBegin');call('virtuappu_render_frame');pixels=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],288*160*4));call('SwitchWide_CenterEnd')
assert bytes(uc.mem_read(symbols['gVram'],0x4000))==before_map,'center presentation changed game VRAM'
assert bytes(uc.mem_read(symbols['gIoMem']+0x10,16))==before_io,'center presentation changed game scroll registers'
black=struct.pack('<I',0xff000000)
for y,x in [(0,4),(0,258),(40,124),(80,16),(100,268)]:assert pixels[(y*288+x)*4:(y*288+x+1)*4]!=black,('centered sprite missing',y,x)
assert pixels[(120*288)*4:(121*288)*4]==black*288,'parked sprite appeared after centering'
call('SwitchOverlay_SetOptions',0,1);call('SwitchWide_Prepare');call('SwitchWide_CenterBegin');assert call('SwitchWide_CenterShift')==0;call('SwitchWide_CenterEnd')
print('PASS: centered sprites enter both margins; HUD/minimap stay anchored; VRAM/IO restore and native menus retain no shift',flush=True)

# A native-width room keeps equal empty borders in every compositor path.
uc.mem_write(symbols['gBgPointersAndDimensions']+24,struct.pack('<H',15));uc.mem_write(symbols['gBg1XPosition'],bytes(2));uc.mem_write(symbols['gBg1YPosition'],bytes(2))
uc.mem_write(symbols['gBgPltt']+30,struct.pack('<H',0x7fff));uc.mem_write(symbols['gVram']+0x4020,bytes([0xff])*32);uc.mem_write(symbols['gVram']+0x1000,struct.pack('<H',1)*2048)
uc.mem_write(symbols['gOamMem'],struct.pack('<4H',0x200,0,0,0)*128)
for name,control,blend,mosaic in [('direct',0x200,0,0),('window',0x2200,0,0),('alpha',0x200,0x2042,0),('mosaic',0x200,0,0x33)]:
 io=bytearray(0x400);struct.pack_into('<H',io,0,control);struct.pack_into('<H',io,10,0x4204|(0x40 if mosaic else 0));struct.pack_into('<H',io,0x40,240);struct.pack_into('<H',io,0x44,160);struct.pack_into('<H',io,0x48,0x3f);struct.pack_into('<H',io,0x4c,mosaic);struct.pack_into('<HH',io,0x50,blend,0x0808)
 uc.mem_write(symbols['gIoMem'],bytes(io));call('SwitchOverlay_SetOptions',0,3);call('SwitchWide_Prepare');call('SwitchWide_CenterBegin');call('virtuappu_render_frame');pixels=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],288*160*4));call('SwitchWide_CenterEnd')
 row=pixels[40*288*4:41*288*4];assert row[:24*4]==black*24 and row[264*4:]==black*24,('small room borders unequal',name)
 assert all(row[x*4:(x+1)*4]!=black for x in range(24,264)),('native room clipped while centered',name)
print('PASS: native-width room centered with equal 24-pixel borders in direct/window/alpha/mosaic paths',flush=True)
