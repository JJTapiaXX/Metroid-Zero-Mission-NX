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
        call('SwitchOverlay_SetOptions',0,3);call('SwitchWide_Prepare');call('virtuappu_render_frame')
        wide=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],288*160*4))
        preserved=b''.join(wide[y*288*4:y*288*4+240*4] for y in range(160))
        assert preserved==native,f'Norfair room {room}, camera {camera}: native background changed'
        (args.output/f'norfair-{room}-{camera}.rgba').write_bytes(wide)
    print('PASS: Norfair room',room,'native pixels and edge camera',flush=True)
