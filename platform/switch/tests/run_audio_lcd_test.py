#!/usr/bin/env python3
"""Native mixer separation, LCD visuals, and compositor instruction measurement."""
from pathlib import Path
script=Path(__file__).with_name('run_game_smoke.py').read_text()
exec(compile(script.split('try:\n    path=allocate(16)')[0],'helpers','exec'))
from unicorn import UC_HOOK_BLOCK
from PIL import Image
with args.elf.open('rb') as stream:
    dwarf=ELFFile(stream).get_dwarf_info()
    def members(name):
        for cu in dwarf.iter_CUs():
            for die in cu.iter_DIEs():
                if die.tag=='DW_TAG_structure_type' and die.attributes.get('DW_AT_name') and die.attributes['DW_AT_name'].value.decode()==name:
                    fields={child.attributes['DW_AT_name'].value.decode():child.attributes['DW_AT_data_member_location'].value for child in die.iter_children() if child.tag=='DW_TAG_member' and 'DW_AT_data_member_location' in child.attributes}
                    if fields:return fields,die.attributes['DW_AT_byte_size'].value
        raise RuntimeError(name)
    channel_fields,channel_size=members('SoundChannel')
path=allocate(16);uc.mem_write(path,b'mzm.gba\0');assert call('GameSwitch_Load',path)
header=allocate(16);uc.mem_write(header,struct.pack('<IIII',0,0,0,4))
samples=allocate(4);uc.mem_write(samples,bytes([16,240,32,224]));channel=allocate(channel_size);mix=allocate(32)
def render(player,boost):
    uc.mem_write(channel,bytes(channel_size));uc.mem_write(mix,bytes(32))
    for field,value in [('unk_1',8),('unk_11',64),('unk_12',32)]:uc.mem_write(channel+channel_fields[field],bytes([value]))
    for field,value in [('pSample',header),('pData',samples),('pVariables',symbols[player])]:uc.mem_write(channel+channel_fields[field],struct.pack('<Q',value))
    call('AudioFilters_Set',6,boost);call('CallSoundCodeA',channel,mix,1)
    return struct.unpack('<8i',uc.mem_read(mix,32))
for player in ('gTrack0Variables','gTrack1Variables','gTrack8Variables'):
    unity=render(player,0);boosted=render(player,4)
    assert unity==(1024,512,-1024,-512,2048,1024,-2048,-1024)
    assert boosted==tuple(value*(1 if player=='gTrack0Variables' else 2) for value in unity)
print('PASS: sampled sound effects doubled; music samples unchanged; signed stereo preserved',flush=True)
# PSG boost retains fractional envelope amplitudes and preserves unity output.
io=symbols['gIoMem']
def psg(gain):
    call('PortPsg_Reset')
    for offset,value in ((0x84,b'\x80'),(0x80,b'\x77\x11\x02'),(0x62,b'\x80\x80'),(0x64,b'\xf8\x87')):uc.mem_write(io+offset,value)
    call('PortPsg_BeginBlock',32768)
    gains=allocate(16);uc.mem_write(gains,struct.pack('<4I',gain,100,100,100));output=allocate(8)
    values=[]
    for _ in range(24):
        call('PortPsg_NextSampleGain',output,output+4,gains);values.append(struct.unpack('<ii',uc.mem_read(output,8)))
    return values
unity=psg(100);boosted=psg(200)
assert any(a or b for a,b in unity)
assert boosted==[(a*2,b*2) for a,b in unity]
print('PASS: PSG voice boost doubled without envelope rounding',flush=True)
main=allocate(24);secondary=allocate(24);overlay=allocate(2);target=allocate(1280*720*4)
secondary_pixels=allocate(320*240*4);uc.mem_write(secondary_pixels,struct.pack('<I',0xffaabbcc)*(320*240))
uc.mem_write(secondary,struct.pack('<QiiQ',secondary_pixels,320,240,320));uc.mem_write(overlay,b'\0\0')
source_pixels=allocate(288*160*4);uc.mem_write(source_pixels,struct.pack('<I',0xff886644)*(288*160))
results={}
for width in (240,288):
    uc.mem_write(main,struct.pack('<QiiQ',source_pixels,width,160,width))
    for layout in (0,2,4):
        call('SwitchOverlay_SetOptions',layout,3)
        golden=None
        for filter in (0,1,2,3,0):
            call('SwitchOverlay_SetLcd',filter)
            assert call('SwitchOverlay_Compose',overlay,target,1280,main,secondary)
            pixels=bytes(uc.mem_read(target,1280*720*4))
            if filter==0:
                if golden is None:golden=pixels
                else:assert golden==pixels
            else:assert pixels!=golden
            if filter==3:
                if layout==2:
                    normal=struct.unpack_from('<I',pixels,(200*1280+900)*4)[0]
                    dimmed=struct.unpack_from('<I',pixels,(200*1280+901)*4)[0]
                else:
                    area=960 if layout==4 else 1280
                    height=min(720,area*160//width);top=(720-height)//2
                    normal=struct.unpack_from('<I',pixels,((top+100)*1280+400)*4)[0]
                    dimmed=struct.unpack_from('<I',pixels,((top+101)*1280+400)*4)[0]
                assert normal==0xff886644 and dimmed==0xff664d33
            if layout:
                bounds=allocate(16);call('SwitchOverlay_SecondBounds',bounds,bounds+4,bounds+8,bounds+12)
                x,y,w,h=struct.unpack('<4i',uc.mem_read(bounds,16))
                assert struct.unpack_from('<I',pixels,(y*1280+x)*4)[0]==0xffaabbcc
            if width==288 and layout==4:
                Image.frombytes('RGBA',(1280,720),pixels).save(args.output/('lcd-'+str(filter)+'.png'))
print('PASS: LCD Off/Soft/Grid/Scanlines at native and Wide widths, rotated gameplay, unchanged UI and exact Off restoration',flush=True)
# Instruction counts characterize CPU work; these are not Switch frame timings.
uc.mem_write(main,struct.pack('<QiiQ',source_pixels,288,160,288));call('SwitchOverlay_SetOptions',4,3)
count=[0]
def block(uc_,address,size,unused):count[0]+=size//4
for filter in (0,1,2,3):
    call('SwitchOverlay_SetLcd',filter);call('SwitchOverlay_Compose',overlay,target,1280,main,secondary)
    count[0]=0;hook_id=uc.hook_add(UC_HOOK_BLOCK,block)
    uc.ctl_flush_tb()
    assert call('SwitchOverlay_Compose',overlay,target,1280,main,secondary)
    uc.hook_del(hook_id);results[str(filter)]=count[0]
(args.output/'compositor-instructions.json').write_text(json.dumps(results,indent=2))
print('Compositor AArch64 instruction counts (Off/Soft/Grid/Scanlines): '+str(results),flush=True)
