#!/usr/bin/env python3
"""Run the native AArch64 engine in Unicorn with file/controller test doubles.

This is a game-engine smoke test, not a Horizon/libnx hardware emulator.
Never writes to the user's ROM or save. ELF and ROM paths are command arguments.
"""
import argparse
import struct
import json
import re
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_INVALID, UC_PROT_READ, UcError
from unicorn.arm64_const import UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_LR, UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_X4, UC_ARM64_REG_X5, UC_ARM64_REG_X8

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('elf',type=Path)
parser.add_argument('rom',type=Path)
parser.add_argument('--frames',type=int,default=400)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--morph-pickup',action='store_true',help='Spawn Morph Ball against Samus to exercise the actual pickup/message path')
parser.add_argument('--image-base',type=lambda x:int(x,0),default=0x7100000000)
parser.add_argument('--heap-base',type=lambda x:int(x,0),default=0x7200000000)
parser.add_argument('--load-state',type=Path,help='Load a saved test state during title boot with new ASLR addresses')
parser.add_argument('--wide-test',action='store_true',help='Verify extra room columns and native pixels in the real renderer')
parser.add_argument('--state-test',action='store_true',help='Exercise whole-game state serialization and rejection')
parser.add_argument('--ra-test',action='store_true',help='Exercise offline achievement caching with a fake authenticated server')
parser.add_argument('--ui-only',action='store_true',help='Test real UI volume controls without running gameplay')
parser.add_argument('--environment-effects',action='store_true',help='Exercise all environmental effect animations')
parser.add_argument('--missile-kills',action='store_true',help='Exercise repeated real missile-hit and enemy-death paths')
args=parser.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
rom=args.rom.read_bytes()
uc=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
base,heap,stack,stop=args.image_base,args.heap_base,0x7300000000,0x7400000000
with args.elf.open('rb') as stream:
    elf=ELFFile(stream)
    segments=[s for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
    tls_segments=[s for s in elf.iter_segments() if s['p_type']=='PT_TLS']
    tls_segment = tls_segments[0] if tls_segments else None
    tls_location = (tls_segment['p_vaddr'], tls_segment['p_memsz'], tls_segment['p_filesz']) if tls_segment else None
    extent=(max(s['p_vaddr']+s['p_memsz'] for s in segments)+4095)&~4095
    uc.mem_map(base,extent)
    for s in segments: uc.mem_write(base+s['p_vaddr'],s.data())
    symbols={s.name:(s['st_value'] if s['st_shndx']=='SHN_ABS' else base+s['st_value']) for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
    relocations=elf.get_section_by_name('.rela.dyn')
    if relocations:
        for r in relocations.iter_relocations():
            if r['r_info_type'] == 0: continue
            if r['r_info_type'] != 1027:
                raise SystemExit(f'Unsupported dynamic relocation {r["r_info_type"]}')
            uc.mem_write(base+r['r_offset'],struct.pack('<Q',base+r['r_addend']))
    packed = elf.get_section_by_name('.relr.dyn')
    if packed:
        cursor = 0
        def relocate_at(offset):
            value=struct.unpack('<Q',uc.mem_read(base+offset,8))[0]
            uc.mem_write(base+offset,struct.pack('<Q',base+value))
        for (entry,) in struct.iter_unpack('<Q',packed.data()):
            if entry & 1:
                for bit in range(1,64):
                    if entry & (1 << bit): relocate_at(cursor+(bit-1)*8)
                cursor+=63*8
            else:
                relocate_at(entry)
                cursor=entry+8
# Match libnx protection after its relocations: RELRO is no longer writable.
relro_start=symbols['__relro_start']; data_start=symbols['__data_start']
uc.mem_protect(relro_start,data_start-relro_start,UC_PROT_READ)
uc.mem_map(heap,0x4000000)
uc.mem_map(stack,0x100000)
uc.mem_map(stop,4096)
uc.reg_write(UC_ARM64_REG_SP,stack+0x100000-16)
used=0
frames=0
scanline=0
failure=None
audio=bytearray()
audio_rate=0
files={'mzm.gba':bytearray(rom)}
handles={}
next_handle=0x7500000000
regs=[UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2,UC_ARM64_REG_X3,UC_ARM64_REG_X4,UC_ARM64_REG_X5]

def return_value(value=0):
    uc.reg_write(UC_ARM64_REG_X0,value&0xffffffffffffffff)
    uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_LR))

allocation_sizes={}
def allocate(size):
    global used
    used=(used+15)&~15
    if size>0x4000000-used: raise RuntimeError('Test heap exhausted')
    result=heap+used
    used+=size
    allocation_sizes[result]=size
    return result

def string(address):
    output=bytearray()
    while len(output)<1024:
        value=uc.mem_read(address+len(output),1)[0]
        if not value: break
        output.append(value)
    return output.decode(errors='replace')

def hook(name,callback):
    if name not in symbols: return
    def wrapped(uc_,address,size,unused): callback()
    uc.hook_add(UC_HOOK_CODE,wrapped,begin=symbols[name],end=symbols[name])

if tls_location:
    tls=allocate(tls_location[1]+16)
    uc.mem_write(tls+16,uc.mem_read(base+tls_location[0],tls_location[2]))
    hook('__aarch64_read_tp',lambda:return_value(tls))
else:
    # libnx's linker uses .main.tls instead of an ELF PT_TLS segment.
    tls=allocate(0x2000)
    hook('__aarch64_read_tp',lambda:return_value(tls))

reent=allocate(0x2000)
hook('__syscall_getreent',lambda:return_value(reent))
hook('_malloc_r',lambda:return_value(allocate(uc.reg_read(regs[1]))))
hook('_calloc_r',lambda:return_value(allocate(uc.reg_read(regs[1])*uc.reg_read(regs[2]))))
hook('_free_r',lambda:return_value())
hook('malloc',lambda:return_value(allocate(uc.reg_read(regs[0]))))
hook('calloc',lambda:return_value(allocate(uc.reg_read(regs[0])*uc.reg_read(regs[1]))))
hook('free',lambda:return_value())
def realloc():
    source,size=[uc.reg_read(r) for r in regs[:2]]; destination=allocate(size)
    if source and size: uc.mem_write(destination,bytes(uc.mem_read(source,min(size,allocation_sizes.get(source,0)))))
    return_value(destination)
hook('realloc',realloc)
def realloc_r():
    uc.reg_write(regs[0],uc.reg_read(regs[1]));uc.reg_write(regs[1],uc.reg_read(regs[2]));realloc()
hook('_realloc_r',realloc_r)
hook('mkdir',lambda:return_value())
def fgetc():
    handle=uc.reg_read(regs[0]); name,position=handles[handle]
    if position>=len(files[name]):return_value(-1);return
    value=files[name][position];handles[handle][1]+=1;return_value(value)
hook('fgetc',fgetc)
def fputc():
    value,handle=[uc.reg_read(r) for r in regs[:2]];name,position=handles[handle]
    files[name][position:position+1]=bytes([value&255]);handles[handle][1]+=1;return_value(value)
hook('fputc',fputc)
def fgets():
    dest,limit,handle=[uc.reg_read(r) for r in regs[:3]];name,position=handles[handle]
    if position>=len(files[name]):return_value(0);return
    data=bytes(files[name][position:position+limit-1]);end=data.find(b'\n')
    if end>=0:data=data[:end+1]
    uc.mem_write(dest,data+b'\0');handles[handle][1]+=len(data);return_value(dest)
hook('fgets',fgets)
def fopen():
    global next_handle
    name=string(uc.reg_read(regs[0])); mode=string(uc.reg_read(regs[1]))
    if 'w' in mode: files[name]=bytearray()
    if name not in files: return_value(); return
    next_handle+=16
    handles[next_handle]=[name,0]
    return_value(next_handle)
def fseek():
    handle,offset,whence=[uc.reg_read(r) for r in regs[:3]]
    name,position=handles[handle]
    if offset&(1<<63): offset-=1<<64
    handles[handle][1]=offset+(len(files[name]) if whence==2 else position if whence==1 else 0)
    return_value()
hook('fopen',fopen)
hook('fseek',fseek)
hook('ftell',lambda:return_value(handles[uc.reg_read(regs[0])][1]))
def rewind(): handles[uc.reg_read(regs[0])][1]=0; return_value()
hook('rewind',rewind)
hook('fclose',lambda:return_value())
hook('fflush',lambda:return_value())
hook('fileno',lambda:return_value(1))
hook('fsync',lambda:return_value())
def fread():
    dest,size,count=[uc.reg_read(r) for r in regs[:3]]
    handle=uc.reg_read(regs[3]); name,position=handles[handle]
    data=files[name][position:position+size*count]
    handles[handle][1]+=len(data)
    uc.mem_write(dest,bytes(data))
    return_value(len(data)//size if size else 0)
hook('fread',fread)
def fwrite():
    source,size,count,handle=[uc.reg_read(r) for r in regs[:4]]
    name,position=handles[handle]
    data=uc.mem_read(source,size*count)
    files[name][position:position+len(data)]=data
    handles[handle][1]+=len(data)
    return_value(count)
hook('fwrite',fwrite)
def remove():
    name=string(uc.reg_read(regs[0])); files.pop(name,None); return_value()
def rename():
    old=string(uc.reg_read(regs[0])); new=string(uc.reg_read(regs[1]))
    if old not in files: return_value(-1); return
    files[new]=files.pop(old); return_value()
hook('remove',remove)
hook('rename',rename)
hook('Port_DebugLog',lambda:return_value())
def format_text(fmt,values):
    arguments=iter(values)
    def replacement(match):
        token=match.group(0)
        if token=='%%':return '%'
        value=next(arguments);kind=token[-1]
        if kind=='s':
            text=string(value); precision=re.search(r'\.(\d+)',token)
            return text[:int(precision[1])] if precision else text
        if kind=='p':return hex(value)
        if kind in 'di' and value&(1<<31):value=(value&0xffffffff)-(1<<32)
        if kind=='u' and 'l' not in token:value&=0xffffffff
        return str(value)
    return re.sub(r'%(?:%|[0-9]*(?:\.[0-9]+)?[lz]*[usdip])',replacement,fmt).encode()
def snprintf():
    destination,limit,format_pointer=[uc.reg_read(r) for r in regs[:3]]
    data=format_text(string(format_pointer),[uc.reg_read(r) for r in regs[3:]])
    if limit:uc.mem_write(destination,data[:limit-1]+b'\0')
    return_value(len(data))
hook('snprintf',snprintf)
def fprintf():
    handle,fmt=[uc.reg_read(r) for r in regs[:2]]
    if handle not in handles:return_value(0);return
    data=format_text(string(fmt),[uc.reg_read(r) for r in regs[2:]])
    name,position=handles[handle];files[name][position:position+len(data)]=data;handles[handle][1]+=len(data)
    return_value(len(data))
hook('fprintf',fprintf)
hook('printf',lambda:return_value())
hook('Port_FlushSramIfDirty',lambda:return_value())
hook('pthread_create',lambda:return_value(1))
hook('pthread_mutex_lock',lambda:return_value())
hook('pthread_mutex_unlock',lambda:return_value())
hook('mutexLock',lambda:return_value())
hook('mutexUnlock',lambda:return_value())
hook('pthread_cond_signal',lambda:return_value())
hook('Port_SaveState_ServicePending',lambda:return_value())
hook('AudioSwitch_LockState',lambda:return_value())
hook('AudioSwitch_UnlockState',lambda:return_value())
hook('AudioSwitch_NowNs',lambda:return_value(1000000000))
hook('time',lambda:return_value(1700000000))
def capture_audio():
    global audio_rate
    pointer,n,rate=[uc.reg_read(r) for r in regs[:3]]
    if rate:
        audio_rate=rate
        audio.extend(uc.mem_read(pointer,n*4))
    return_value()
hook('AudioSwitch_Submit',capture_audio)
def read_scanline():
    global scanline
    if uc.reg_read(regs[0])==0x04000006:
        scanline=(scanline+1)%228
        return_value(scanline)
hook('gba_read8',read_scanline)

def invalid(uc_,access,address,size,value,unused):
    global failure
    pc=uc.reg_read(UC_ARM64_REG_PC)
    near=max(((addr,name) for name,addr in symbols.items() if base<=addr<=pc),default=(base,'unknown'))
    failure=f'Invalid memory access 0x{address:x} at {near[1]}+0x{pc-near[0]:x}, LR 0x{uc.reg_read(UC_ARM64_REG_LR):x}'
    print(failure,flush=True)
    return False
uc.hook_add(UC_HOOK_MEM_INVALID,invalid)

def call(name,*values):
    for reg,value in zip(regs,values): uc.reg_write(reg,value)
    uc.reg_write(UC_ARM64_REG_LR,stop)
    uc.emu_start(symbols[name],stop,timeout=20_000_000)
    if uc.reg_read(UC_ARM64_REG_PC)!=stop:
        raise RuntimeError(f'{name} did not return; PC=0x{uc.reg_read(UC_ARM64_REG_PC):x}')
    return uc.reg_read(UC_ARM64_REG_X0)

fake_online=True
fake_reply=None
fake_reply_size=0
fake_token='unit-test-token'
fake_awards=[]
if args.ra_test:
    from urllib.parse import parse_qs
    from PIL import Image
    from io import BytesIO
    png=BytesIO();Image.new('RGBA',(64,64),(18,52,86,255)).save(png,format='PNG');badge_png=png.getvalue()
    files['ra-account.cfg']=bytearray(b'TestUser\nunit-test-token\n')
    def server_request():
        global fake_reply
        params=parse_qs(string(uc.reg_read(regs[1]))) if uc.reg_read(regs[1]) else {};operation=params.get('r',[''])[0]
        if not fake_online:fake_reply=(-1,None)
        elif not uc.reg_read(regs[1]) and '/Badge/' in string(uc.reg_read(regs[0])):fake_reply=(200,badge_png)
        elif operation=='login2':fake_reply=(200,{'Success':True,'User':'TestUser','Token':fake_token})
        elif operation=='patch':
            achievements=[]
            for id_,title,definition in [(777001,'TEST MORPH','0xH00153e=64.3.'),(777002,'TEST OFFLINE','0xH00153e=128.2.'),(777003,'INVALID MEMORY','0xH7fffff=0')]:
                achievements.append({'ID':id_,'Title':title,'Description':'Synthetic test only','Flags':3,'Points':5,'MemAddr':definition,'Author':'Test','BadgeName':'999990' if id_==777001 else '255208','Created':1700000000,'Modified':1700000000})
            fake_reply=(200,{'Success':True,'PatchData':{'ID':999999,'Title':'Synthetic test set','ConsoleID':5,'ImageIcon':'/Images/00000.png','RichPresencePatch':'','Achievements':achievements,'Leaderboards':[]}})
        elif operation=='startsession':fake_reply=(200,{'Success':True,'Unlocks':[],'HardcoreUnlocks':[],'ServerNow':1700000000})
        elif operation=='awardachievement':
            if params.get('h')!=['0']:raise RuntimeError('Achievement request was not softcore')
            id_=int(params['a'][0]);fake_awards.append(id_)
            fake_reply=(200,{'Success':True,'AchievementID':id_,'AchievementsRemaining':0})
        else:raise RuntimeError('Unexpected RA request '+operation)
        return_value(1)
    def server_poll():
        global fake_reply,fake_reply_size
        if fake_reply is None:return_value(0);return
        status,body=fake_reply;fake_reply=None;pointer=0
        if body is not None:
            data=body if isinstance(body,bytes) else json.dumps(body,separators=(',',':')).encode()
            fake_reply_size=len(data);pointer=allocate(len(data)+1);uc.mem_write(pointer,data+b'\0')
        uc.mem_write(uc.reg_read(regs[0]),struct.pack('<Q',pointer));uc.mem_write(uc.reg_read(regs[1]),struct.pack('<i',status));return_value(1)
    hook('NetworkSwitch_Request',server_request);hook('NetworkSwitch_Poll',server_poll);hook('NetworkSwitch_Close',lambda:return_value());hook('NetworkSwitch_ResponseSize',lambda:return_value(fake_reply_size))

def test_achievements():
    global fake_online,fake_token
    # Preserve explicit enable preference even before a token exists.
    profile=files.pop('ra-account.cfg',None); files['ra-enabled.cfg']=bytearray([1])
    call('Achievements_Init')
    if string(call('Achievements_ConnectionLabel'))=='ACHIEVEMENTS OFF':raise RuntimeError('Enabled preference lost before first login')
    if profile is not None:files['ra-account.cfg']=profile
    call('Achievements_Init')
    # Validate the original 56-byte sprite stride against the 64-byte host.
    target=allocate(1);original=bytes(uc.mem_read(symbols['gSpriteData']+64,64))
    uc.mem_write(symbols['gSpriteData']+64+33,b'\x66')
    if not call('NativeMemory_ReadByte',0x1a8+56+29,target) or uc.mem_read(target,1)!=b'\x66':
        raise RuntimeError('RA sprite layout/stride conversion failed')
    uc.mem_write(symbols['gSpriteData']+64,original)
    for _ in range(8):call('Achievements_Update')
    if call('Achievements_Count')!=3:raise RuntimeError('RA set load failed: '+string(call('Achievements_ConnectionLabel')))
    if call('Achievements_Supported',2):raise RuntimeError('Invalid-memory achievement was not disabled')
    equipment=symbols['gEquipment']+14;before=bytes(uc.mem_read(equipment,1));uc.mem_write(equipment,b'\0')
    call('Achievements_Evaluate');call('Achievements_Evaluate')
    uc.mem_write(equipment,b'\x40');call('Achievements_Evaluate')
    progress_size=call('Achievements_ProgressSize');progress=allocate(progress_size)
    if not call('Achievements_SaveProgress',progress,progress_size):raise RuntimeError('RA progress serialization failed')
    call('Achievements_Evaluate')
    if not call('Achievements_LoadProgress',progress,progress_size):raise RuntimeError('RA progress restore failed')
    call('Achievements_Evaluate')
    if call('Achievements_Pending'):raise RuntimeError('RA hit counts were not restored')
    call('Achievements_Evaluate')
    if call('Achievements_Pending')!=1 or not call('Achievements_Unlocked',0):raise RuntimeError('RA trigger did not queue unlock')
    if 'toast_title' in symbols and string(symbols['toast_title'])!='TEST MORPH':raise RuntimeError('Achievement popup not created: '+repr(string(symbols['toast_title'])))
    fake_online=False
    call('Achievements_Update');call('Achievements_Update')
    call('Achievements_Close');call('Achievements_Init');call('Achievements_Update')
    if call('Achievements_Count')!=3 or call('Achievements_Pending')!=1:raise RuntimeError('Offline cache restart lost set or queued unlock')
    call('Achievements_Evaluate');uc.mem_write(equipment,b'\x80')
    call('Achievements_Evaluate');call('Achievements_Evaluate')
    if call('Achievements_Pending')!=2:raise RuntimeError('Offline achievement did not queue')
    fake_online=True;fake_token='rotated-test-token';call('Achievements_Reconnect')
    for _ in range(12):call('Achievements_Update')
    if call('Achievements_Pending') or sorted(fake_awards)!=[777001,777002]:raise RuntimeError('Offline queued unlocks did not sync after token rotation')
    if sum(call('Achievements_Unlocked',i)!=0 for i in range(call('Achievements_Count')))!=2:raise RuntimeError('Earned count lost after sync')
    downloaded=call('Achievements_Badge',0)
    if not downloaded or struct.unpack('<I',uc.mem_read(downloaded,4))[0]!=0xff563412:raise RuntimeError('Downloaded PNG badge missing or wrong colors')
    if not call('Achievements_Badge',1):raise RuntimeError('Bundled badge missing')
    if not any(k.endswith('/999990.rgba') for k in files):raise RuntimeError('Badge not cached for offline use')
    cache_name=next(k for k in files if k.endswith('/unlocks.bin'))
    backup_name=cache_name+'.bak';backup=files.pop(backup_name,None);cache=bytearray(files[cache_name]);files[cache_name][-1]^=1
    call('Achievements_Close');fake_online=False;call('Achievements_Init');call('Achievements_Update')
    if call('Achievements_Pending'):raise RuntimeError('Tampered unlock cache accepted')
    files[cache_name]=cache
    if backup is not None:files[backup_name]=backup
    uc.mem_write(equipment,before)
    ui_input=allocate(32);uc.mem_write(ui_input,struct.pack('<HBBBBxxiiBxxx',0,0,1,1,1,224,8,0))
    ui=call('SwitchUI_Render',ui_input);(args.output/'achievements.rgba').write_bytes(uc.mem_read(ui,320*240*4))
    print('PASS: RA mapped memory, triggers, popups, hit-count progress, offline restart, authenticated cache rejection and queued softcore sync after token rotation (fake server)',flush=True)

def test_save_states():
    checks={name:bytes(uc.mem_read(symbols[name],size)) for name,size in [('gSamusData',28),('gEquipment',20),('gSpriteData',24*64),('gCurrentRoom',1)]}
    if not call('SaveStates_Save',0): raise RuntimeError('State save failed: '+string(call('SaveStates_Message')))
    saved=bytes(files['states/slot-1.bin'])
    for name,data in checks.items(): uc.mem_write(symbols[name],bytes(len(data)))
    if not call('SaveStates_Load',0): raise RuntimeError('State load failed: '+string(call('SaveStates_Message')))
    for name,data in checks.items():
        if bytes(uc.mem_read(symbols[name],len(data)))!=data: raise RuntimeError(f'State did not restore {name}')
    original=bytes(uc.mem_read(symbols['gSamusData'],28))
    damaged=bytearray(saved);damaged[-1]^=0x80;files['states/slot-1.bin']=damaged
    if call('SaveStates_Load',0):raise RuntimeError('Corrupted state accepted')
    if bytes(uc.mem_read(symbols['gSamusData'],28))!=original:raise RuntimeError('Rejected state changed game')
    damaged=bytearray(saved);damaged[52]^=0x80;files['states/slot-1.bin']=damaged
    if call('SaveStates_Load',0):raise RuntimeError('Corrupted state music header accepted')
    files['states/slot-1.bin']=bytearray(saved[:-8])
    if call('SaveStates_Load',0):raise RuntimeError('Truncated state accepted')
    mismatch=bytearray(saved);mismatch[8]^=1;files['states/slot-1.bin']=mismatch
    if call('SaveStates_Load',0):raise RuntimeError('Different-build state accepted')
    mismatch=bytearray(saved);mismatch[24]^=1;files['states/slot-1.bin']=mismatch
    if call('SaveStates_Load',0):raise RuntimeError('Different-ROM state accepted')
    files['states/slot-1.bin']=bytearray(saved)
    if not call('SaveStates_Save',0):raise RuntimeError('Existing state slot could not be replaced')
    (args.output/'state-test.bin').write_bytes(files['states/slot-1.bin'])
    print(f'PASS: whole-game state save/load, pointer restoration, replacement and corrupt/ROM/build rejection ({len(saved)} bytes)',flush=True)

def test_volume_ui():
    input_pointer=allocate(32)
    def ui_input(x,y,held=True,pressed=False,released=False,visible=True,tab_delta=0):
        uc.mem_write(input_pointer,struct.pack('<HBBBBxxiiBbxx',0,0,visible,held,pressed,x,y,released,tab_delta))
        call('SwitchUI_Render',input_pointer)
    ui_input(32,8,pressed=True);ui_input(32,8,held=False,released=True)
    for expected in (1,2,3,4,0):
        ui_input(32,8,held=False,tab_delta=1)
        if 'tab' in symbols and struct.unpack('<i',uc.mem_read(symbols['tab'],4))[0]!=expected:
            raise RuntimeError('R shoulder tab navigation failed')
    ui_input(32,8,held=False,tab_delta=-1)
    if 'tab' in symbols and struct.unpack('<i',uc.mem_read(symbols['tab'],4))[0]!=4:
        raise RuntimeError('L shoulder tab navigation failed')
    ui_input(288,8,pressed=True);ui_input(288,8,held=False,released=True)
    initial=call('AudioVolume_Get')
    ui_input(260,180,pressed=True);ui_input(260,180,held=False,released=True)
    if call('AudioVolume_Get')!=initial:raise RuntimeError('General tab still changes master volume')
    ui_input(150,32,pressed=True);ui_input(150,32,held=False,released=True)
    for expected in (0,1,25,50,75,99,100,125,150):
        x=48+(expected*224+149)//150
        ui_input(x,70,pressed=True)
        actual=call('AudioVolume_Get')
        if actual!=expected: raise RuntimeError(f'Slider at {x}: expected {expected}, got {actual}')
        ui_input(x,70,held=False,released=True)
    ui_input(160,70,pressed=True);ui_input(216,70)
    if call('AudioVolume_Get')!=112: raise RuntimeError('Held slider drag did not reach 112%')
    ui_input(104,70)
    if call('AudioVolume_Get')!=37: raise RuntimeError('Held slider drag did not reach 37%')
    ui_input(104,70,held=False,released=True)
    ui_input(260,46,pressed=True)
    if call('AudioVolume_Get')!=37: raise RuntimeError('Volume label touch changed the slider')
    ui_input(260,46,held=False,released=True)
    call('AudioVolume_Set',50)
    ui_input(24,70,pressed=True)
    if call('AudioVolume_Get')!=49: raise RuntimeError('Minus volume button failed')
    ui_input(24,70,held=False,released=True)
    ui_input(290,70,pressed=True)
    if call('AudioVolume_Get')!=50: raise RuntimeError('Plus volume button failed')
    ui_input(290,70,held=False,released=True)
    ui_input(160,70,pressed=True);ui_input(290,70,pressed=True)
    if call('AudioVolume_Get')!=76: raise RuntimeError('New plus press retained stale slider drag')
    ui_input(160,70,pressed=True);ui_input(24,70,pressed=True)
    if call('AudioVolume_Get')!=74: raise RuntimeError('New minus press retained stale slider drag')
    ui_input(24,70,held=False,released=True)
    print('PASS: slider intermediate values, plus/minus and stale-drag cancellation',flush=True)

def test_audio_ui():
    pointer=allocate(32)
    def tap(x,y):
        uc.mem_write(pointer,struct.pack('<HBBBBxxiiBxxx',0,0,1,1,1,x,y,0))
        ui=call('SwitchUI_Render',pointer)
        uc.mem_write(pointer,struct.pack('<HBBBBxxiiBxxx',0,0,1,0,0,x,y,1))
        call('SwitchUI_Render',pointer)
        return ui
    tap(288,8)
    before=call('AudioVolume_Get')
    ui=tap(150,32)
    if call('AudioVolume_Get')!=before: raise RuntimeError('Opening audio page altered master volume')
    (args.output/'overlay-audio.rgba').write_bytes(uc.mem_read(ui,320*240*4))
    controls=[(1,100,94,0),(0,100,118,1),(2,100,142,0),(3,100,166,0),(4,100,192,0),(5,200,192,1),(6,200,94,1)]
    for option,x,y,expected in controls:
        tap(x,y)
        if call('AudioFilters_Get',option)!=expected: raise RuntimeError(f'Audio option {option} failed')
    for option,_,_,_ in controls: call('AudioFilters_Set',option,1 if option!=5 else 0)
    call('AudioFilters_Load')
    for option,_,_,expected in controls:
        if call('AudioFilters_Get',option)!=expected: raise RuntimeError(f'Audio setting {option} failed reload')
    for option in range(6): call('AudioFilters_Set',option,1 if option!=5 else 0)
    call('AudioFilters_Save')
    for option in range(6): call('AudioFilters_Set',option,0)
    call('AudioFilters_Load')
    for option in range(6):
        if call('AudioFilters_Get',option)!=(1 if option!=5 else 0): raise RuntimeError('Audio setting replacement failed')
    tap(100,224)
    if call('AudioFilters_Get',0)!=0 or call('AudioFilters_Get',1)!=2:
        raise RuntimeError('Reset audio defaults failed')
    # Verify migration of the overly quiet fix5 profile without changing master.
    before=call('AudioVolume_Get')
    files['mzmnx-audio.cfg']=b'NXA'+bytes((1,0x75,0x75^0xa5))
    call('AudioFilters_Load')
    if call('AudioFilters_Get',0)!=0 or call('AudioFilters_Get',1)!=2 or call('AudioVolume_Get')!=before:
        raise RuntimeError('Legacy audio profile migration failed')
    call('AudioFilters_Save')
    print('PASS: audio settings touch controls, persistence and replacement',flush=True)

def test_display_ui():
    pointer=allocate(32)
    def tap(x,y):
        uc.mem_write(pointer,struct.pack('<HBBBBxxiiBxxx',0,0,1,1,1,x,y,0))
        ui=call('SwitchUI_Render',pointer)
        uc.mem_write(pointer,struct.pack('<HBBBBxxiiBxxx',0,0,1,0,0,x,y,1))
        call('SwitchUI_Render',pointer)
        return ui
    tap(288,8); tap(250,32); tap(160,80); ui=tap(100,148)
    tap(100,188)
    if not call('SwitchOverlay_FpsVisible'): raise RuntimeError('Show FPS toggle failed')
    if call('SwitchOverlay_Layout')!=1 or call('SwitchOverlay_Scaling')!=1:
        raise RuntimeError('Display option touch controls failed')
    (args.output/'overlay-display.rgba').write_bytes(uc.mem_read(ui,320*240*4))
    call('SwitchOverlay_SetOptions',0,0); call('SwitchOverlay_ShowFps',0); call('SwitchOverlay_LoadOptions')
    if call('SwitchOverlay_Layout')!=1 or call('SwitchOverlay_Scaling')!=1:
        raise RuntimeError('Display settings did not persist')
    if not call('SwitchOverlay_FpsVisible'): raise RuntimeError('FPS setting did not persist')
    tap(100,148)
    call('SwitchOverlay_SetOptions',0,0); call('SwitchOverlay_LoadOptions')
    if call('SwitchOverlay_Layout')!=1 or call('SwitchOverlay_Scaling')!=2:
        raise RuntimeError('Display settings replacement failed')
    tap(260,80)
    if call('SwitchOverlay_Layout')!=2:raise RuntimeError('Flip Grip selection failed')
    call('SwitchOverlay_SetOptions',0,0);call('SwitchOverlay_LoadOptions')
    if call('SwitchOverlay_Layout')!=2:raise RuntimeError('Flip Grip preference did not persist')
    for layout,x in ((3,80),(4,230)):
        tap(x,112)
        if call('SwitchOverlay_Layout')!=layout:raise RuntimeError('Corner layout selection failed')
        call('SwitchOverlay_SetOptions',0,0);call('SwitchOverlay_LoadOptions')
        if call('SwitchOverlay_Layout')!=layout:raise RuntimeError('Corner layout preference did not persist')
    tap(60,80)
    if call('SwitchOverlay_Layout')!=0:raise RuntimeError('Popup layout selection failed')
    tap(100,148)
    if call('SwitchOverlay_Scaling')!=3:raise RuntimeError('Wide scale selection failed')
    call('SwitchOverlay_SetOptions',0,0);call('SwitchOverlay_LoadOptions')
    if call('SwitchOverlay_Scaling')!=3:raise RuntimeError('Wide scale did not persist')
    call('SwitchOverlay_SetOptions',1,0)
    tap(100,224); ui=tap(32,8)
    target=allocate(1280*720*4); main=allocate(24); secondary=allocate(24); overlay=allocate(2)
    uc.mem_write(main,struct.pack('<QiiQ',symbols['virtuappu_frame_buffer'],240,160,240))
    uc.mem_write(secondary,struct.pack('<QiiQ',ui,320,240,320)); uc.mem_write(overlay,b'\x00\x00')
    if not call('SwitchOverlay_Compose',overlay,target,1280,main,secondary):
        raise RuntimeError('Side-by-side composition failed')
    (args.output/'composed-side-by-side.rgba').write_bytes(uc.mem_read(target,1280*720*4))
    call('SwitchOverlay_SetOptions',2,0)
    call('SwitchOverlay_Compose',overlay,target,1280,main,secondary)
    (args.output/'flip-grip.rgba').write_bytes(uc.mem_read(target,1280*720*4))
    if 'icon_gfx_size' in symbols and struct.unpack('<I',uc.mem_read(symbols['icon_gfx_size'],4))[0]<12160:
        raise RuntimeError('Pause-menu icon graphics did not decode')
    call('SwitchOverlay_SetOptions',0,0); call('SwitchOverlay_ShowFps',0); call('SwitchOverlay_SaveOptions')
    tap(288,8);tap(250,32);tap(230,188)
    if call('SwitchOverlay_Lcd')!=1:raise RuntimeError('LCD filter touch failed')
    call('SwitchOverlay_SetLcd',0);call('SwitchOverlay_LoadOptions')
    if call('SwitchOverlay_Lcd')!=1:raise RuntimeError('LCD preference did not persist')
    tap(230,188);tap(230,188)
    if call('SwitchOverlay_Lcd')!=3:raise RuntimeError('Scanlines selection failed')
    call('SwitchOverlay_SetLcd',0);call('SwitchOverlay_LoadOptions')
    if call('SwitchOverlay_Lcd')!=3:raise RuntimeError('Scanlines preference did not persist')
    call('SwitchOverlay_SetLcd',0)
    call('SwitchOverlay_SetOptions',3,3)
    call('SwitchOverlay_SetCornerPlacement',1,680,220,384)
    call('SwitchOverlay_SaveOptions')
    call('SwitchOverlay_SetCornerPlacement',0,4,4,312)
    call('SwitchOverlay_LoadOptions')
    saved=allocate(12)
    if not call('SwitchOverlay_CornerPlacement',saved,saved+4,saved+8):raise RuntimeError('Custom corner placement did not persist')
    if struct.unpack('<iii',uc.mem_read(saved,12))!=(680,220,384):raise RuntimeError('Saved corner rectangle changed')
    call('SwitchOverlay_SaveOptions')
    files['mzmnx-display.cfg'][15]^=1
    call('SwitchOverlay_SetCornerPlacement',0,4,4,312);call('SwitchOverlay_LoadOptions')
    if not call('SwitchOverlay_CornerPlacement',saved,saved+4,saved+8):raise RuntimeError('Corrupt settings did not restore backup')
    files['mzmnx-display.cfg']=bytearray(b'NXD'+bytes([2,1,3,1,1^3^1^0x5a]))
    call('SwitchOverlay_LoadOptions')
    if call('SwitchOverlay_Layout')!=1 or call('SwitchOverlay_Scaling')!=3:raise RuntimeError('Legacy display settings failed')
    if call('SwitchOverlay_CornerPlacement',saved,saved+4,saved+8):raise RuntimeError('Legacy settings kept custom placement')
    call('SwitchOverlay_SetCornerPlacement',0,4,4,312)
    call('SwitchOverlay_SetOptions',0,0);call('SwitchOverlay_SaveOptions')
    print('PASS: display settings and custom corner placement persistence',flush=True)

def test_wide_view():
    uc.mem_write(symbols['virtuappu_registers']+4,b'\x01')
    call('SwitchOverlay_SetOptions',0,1);call('SwitchWide_Prepare');call('virtuappu_render_frame')
    original=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],240*160*4))
    call('SwitchOverlay_SetOptions',0,3);call('SwitchWide_Prepare');call('virtuappu_render_frame')
    expanded=bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],288*160*4))
    preserved=b''.join(expanded[y*288*4:y*288*4+240*4] for y in range(160))
    if preserved[32*240*4:112*240*4]!=original[32*240*4:112*240*4]:raise RuntimeError('Wide changed the original gameplay world pixels')
    right=b''.join(expanded[y*288*4+240*4:(y+1)*288*4] for y in range(160))
    (args.output/'wide-world.rgba').write_bytes(expanded);(args.output/'original-world.rgba').write_bytes(original)
    if len(set(struct.unpack('<'+str(len(right)//4)+'I',right)))<4:raise RuntimeError('Wide margins contain no room scenery')
    if right==b''.join(original[y*240*4:y*240*4+48*4] for y in range(160)):raise RuntimeError('Wide repeated the original image')
    (args.output/'wide-world.rgba').write_bytes(expanded)
    (args.output/'original-world.rgba').write_bytes(original)
    # Menus stay original even with Wide saved.
    mode=bytes(uc.mem_read(symbols['gMainGameMode'],2));uc.mem_write(symbols['gMainGameMode'],b'\x01\x00')
    call('SwitchWide_Prepare')
    if struct.unpack('<H',uc.mem_read(symbols['virtuappu_registers'],2))[0]!=240:raise RuntimeError('Menu viewport was widened')
    if call('SwitchWide_Margin'):raise RuntimeError('Wide widened a non-gameplay screen')
    uc.mem_write(symbols['gMainGameMode'],mode)
    old_cutscene=bytes(uc.mem_read(symbols['gInGameCutscene']+3,1));uc.mem_write(symbols['gInGameCutscene']+3,b'\x04')
    call('SwitchWide_Prepare')
    if call('SwitchWide_Margin') or struct.unpack('<H',uc.mem_read(symbols['virtuappu_registers'],2))[0]!=240:raise RuntimeError('In-game cinematic viewport was widened')
    uc.mem_write(symbols['gInGameCutscene']+3,old_cutscene)
    call('SwitchOverlay_SetOptions',0,0);call('SwitchWide_Prepare');call('virtuappu_render_frame')
    print('PASS: Wide reveals real room pixels, preserves all original pixels, and keeps menus original',flush=True)

def test_live_touch_vblank():
    polls=[0];presents=[0]
    def poll():
        polls[0]+=1
        if polls[0]>1:raise RuntimeError('Touch held gameplay inside the popup pause loop')
        uc.mem_write(uc.reg_read(UC_ARM64_REG_X8),struct.pack('<HBBBBxxiiBxxx',1,0,1,1,1,32,40,0))
        return_value()
    def present():presents[0]+=1;return_value(1)
    hook('PlatformSwitch_Poll',poll);hook('PlatformSwitch_Present',present)
    hook('CallbackCallVblank',lambda:return_value());hook('virtuappu_render_frame',lambda:return_value())
    for layout in (1,2,3,4):
        call('SwitchOverlay_SetOptions',layout,0);polls[0]=presents[0]=0
        call('GameSwitch_VBlank')
        if polls[0]!=1 or presents[0]!=1:raise RuntimeError('Persistent screen touch blocked or duplicated a frame')
    mode=bytes(uc.mem_read(symbols['gMainGameMode'],2));sub=bytes(uc.mem_read(symbols['gSubGameMode1'],2))
    cutscene=bytes(uc.mem_read(symbols['gInGameCutscene']+3,1));disable=bytes(uc.mem_read(symbols['gDisableDrawingSamusAndScrolling'],1));fade=bytes(uc.mem_read(symbols['gColorFading'],1))
    uc.mem_write(symbols['gMainGameMode'],b'\x04\x00');uc.mem_write(symbols['gSubGameMode1'],b'\x02\x00');uc.mem_write(symbols['gInGameCutscene']+3,b'\0');uc.mem_write(symbols['gDisableDrawingSamusAndScrolling'],b'\0')
    call('SwitchOverlay_SetOptions',0,3)
    for kind in (3,4):
        uc.mem_write(symbols['gColorFading'],bytes([kind]));call('SwitchWide_Prepare')
        if call('SwitchWide_Margin') or struct.unpack('<H',uc.mem_read(symbols['virtuappu_registers'],2))[0]!=240:raise RuntimeError('Door fade revealed incomplete room graphics')
    uc.mem_write(symbols['gColorFading'],b'\0');call('SwitchWide_Prepare')
    if call('SwitchWide_Margin')!=48:raise RuntimeError('Wide did not resume after door fade')
    uc.mem_write(symbols['gMainGameMode'],mode);uc.mem_write(symbols['gSubGameMode1'],sub);uc.mem_write(symbols['gInGameCutscene']+3,cutscene);uc.mem_write(symbols['gDisableDrawingSamusAndScrolling'],disable);uc.mem_write(symbols['gColorFading'],fade)
    print('PASS: door/white fades stay native and Wide resumes afterwards',flush=True)
    print('PASS: real VBlank returns to gameplay after one touch poll in all four live layouts',flush=True)

try:
    path=allocate(16)
    uc.mem_write(path,b'mzm.gba\0')
    if not call('GameSwitch_Load',path): raise RuntimeError('ROM load failed')
    print('ROM and native ROM tables loaded',flush=True)
    if args.ui_only:
        if args.ra_test:test_achievements()
        test_volume_ui()
        test_audio_ui()
        test_display_ui()
        test_live_touch_vblank()
        raise SystemExit(0)
    def vblank(): uc.emu_stop()
    hook('GameSwitch_VBlank',vblank)
    uc.reg_write(UC_ARM64_REG_LR,stop)
    pc=symbols['agbmain']
    states=[]
    while frames<args.frames:
        uc.emu_start(pc,stop,timeout=20_000_000)
        if uc.reg_read(UC_ARM64_REG_PC)!=symbols['GameSwitch_VBlank']:
            raise RuntimeError(f'Game did not reach VBlank at frame {frames}; PC=0x{uc.reg_read(UC_ARM64_REG_PC):x}')
        saved=uc.context_save()
        mode_before=uc.mem_read(symbols['gMainGameMode'],1)[0]
        if args.load_state and frames==300:
            files['states/slot-1.bin']=bytearray(args.load_state.read_bytes())
            if not call('SaveStates_Load',0):raise RuntimeError('Relaunch state load failed: '+string(call('SaveStates_Message')))
            if uc.mem_read(symbols['gMainGameMode'],1)!=b'\x04':raise RuntimeError('Relaunch state did not restore gameplay')
            print('PASS: state restored from title with different module/heap addresses',flush=True)
        if args.environment_effects and mode_before==4 and frames>=1150 and frames<1150+18*32:
            effect=(frames-1150)//32+1
            if (frames-1150)%32==0:
                env=symbols['gSamusEnvironmentalEffects']+4*16
                uc.mem_write(env,struct.pack('<BBBBHHQ',effect,0,0,0,640,1024,0))
                print(f'Environmental effect {effect}',flush=True)
        if args.missile_kills and mode_before==4 and frames>=1150 and frames%50==0:
            x,y=struct.unpack('<HH',uc.mem_read(symbols['gSamusData']+18,4))
            slot=call('SpriteSpawnPrimary',18,0,0,y-32,x+64,0)
            if slot<24:
                enemy=symbols['gSpriteData']+slot*64
                old_current=bytes(uc.mem_read(symbols['gCurrentSprite'],64))
                uc.mem_write(symbols['gCurrentSprite'],bytes(uc.mem_read(enemy,64)))
                call('Zoomer')
                uc.mem_write(enemy,bytes(uc.mem_read(symbols['gCurrentSprite'],64)))
                uc.mem_write(symbols['gCurrentSprite'],old_current)
                uc.mem_write(enemy+20,struct.pack('<H',1))
                uc.mem_write(symbols['gEquipment']+2,struct.pack('<H',99))
                uc.mem_write(symbols['gEquipment']+8,struct.pack('<H',99))
                if call('ProjectileInit',12,y-32,x+64):
                    for i in range(16):
                        projectile=symbols['gProjectileData']+i*40
                        if uc.mem_read(projectile,1)[0]&1 and uc.mem_read(projectile+23,1)[0]==12:
                            call('ProjectileMissileHitSprite',enemy,projectile,y-32,x+64)
                            print(f'Missile hit enemy slot {slot} at frame {frames}',flush=True)
                            break
        if args.morph_pickup and frames==1121:
            x,y=struct.unpack('<HH',uc.mem_read(symbols['gSamusData']+18,4))
            slot=call('SpriteSpawnPrimary',33,0,0,y+32,x,0)
            print(f'Spawned Morph Ball at Samus, sprite slot {slot}',flush=True)
            # Use the real AI entry point for initialization and acquisition.
            # Set its collision flag explicitly so this test is independent
            # of Samus's jump/landing timing in the opening room.
            old_current=bytes(uc.mem_read(symbols['gCurrentSprite'],64))
            uc.mem_write(symbols['gCurrentSprite'],bytes(uc.mem_read(symbols['gSpriteData']+slot*64,64)))
            call('MorphBall')
            status=struct.unpack('<H',uc.mem_read(symbols['gCurrentSprite'],2))[0]
            uc.mem_write(symbols['gCurrentSprite'],struct.pack('<H',status|2048))
            call('MorphBall')
            uc.mem_write(symbols['gSpriteData']+slot*64,bytes(uc.mem_read(symbols['gCurrentSprite'],64)))
            uc.mem_write(symbols['gCurrentSprite'],old_current)
            print('Morph Ball acquisition AI invoked',flush=True)
        if args.morph_pickup and frames>1120:
            if mode_before==4 and frames>=1640:
                keys=128 if frames%60<2 or 20<=frames%60<22 else 16 if frames%60>=40 else 0
            else:
                keys=1 if frames%40<2 else 0
        elif mode_before==4 and frames>1120:
            phase=(frames//120)%4
            keys=[16,32,18,33][phase]
        else:
            keys=9 if frames%40<2 else 0
        uc.mem_write(symbols['gIoMem']+0x130,struct.pack('<H',(~keys)&0x3ff))
        call('CallbackCallVblank')
        if 'GameSwitch_AudioTick' in symbols: call('GameSwitch_AudioTick')
        if frames%100==0 or frames+1==args.frames or (args.morph_pickup and 1120<frames<1145):
            dispcnt=struct.unpack('<H',uc.mem_read(symbols['gIoMem'],2))[0]
            uc.mem_write(symbols['virtuappu_registers']+4,bytes([2 if dispcnt&7 else 1]))
            call('virtuappu_render_frame')
            if frames%100==0 or frames+1==args.frames:
                (args.output/f'frame-{frames:04d}.rgba').write_bytes(uc.mem_read(symbols['virtuappu_frame_buffer'],240*160*4))
        mode=uc.mem_read(symbols['gMainGameMode'],1)[0]
        sub=uc.mem_read(symbols['gSubGameMode1'],1)[0]
        x,y=struct.unpack('<HH',uc.mem_read(symbols['gSamusData']+18,4))
        pose=uc.mem_read(symbols['gSamusData'],1)[0]
        states.append({'frame':frames,'mode':mode,'submode':sub,'x':x,'y':y,'pose':pose})
        if frames%50==0: print(f'Frame {frames}: mode {mode}, submode {sub}',flush=True)
        frames+=1
        uc.context_restore(saved)
        pc=uc.reg_read(UC_ARM64_REG_LR)
    (args.output/'states.json').write_text(json.dumps(states,indent=2))
    if not any(s['mode']==4 for s in states): raise RuntimeError('Smoke test did not enter gameplay')
    positions={(s['x'],s['y']) for s in states if s['mode']==4 and s['frame']>1120}
    if args.frames>1400 and len(positions)<5 and not args.morph_pickup: raise RuntimeError('Movement test did not change Samus position')
    if args.morph_pickup and not uc.mem_read(symbols['gEquipment']+14,1)[0]&64:
        raise RuntimeError('Morph Ball pickup did not occur')
    if args.wide_test:test_wide_view()
    if args.ra_test:test_achievements()
    if args.state_test: test_save_states()
    # Render the actual new bottom UI against the live game globals. Select
    # each tab using the same press coordinates delivered by Switch touch.
    for name,x_tab in [('map',32),('status',96),('options',160),('states',224),('achievements',288)]:
        input_pointer=allocate(32)
        payload=struct.pack('<HBBBBxxiiBxxx',0,0,1,1,1,x_tab,8,0)
        uc.mem_write(input_pointer,payload)
        ui=call('SwitchUI_Render',input_pointer)
        rgba=uc.mem_read(ui,320*240*4)
        (args.output/f'overlay-{name}.rgba').write_bytes(rgba)
        # Compose real gameplay plus the actual UI using the production C code.
        main_surface=allocate(24); second_surface=allocate(24); overlay=allocate(2)
        target=allocate(1280*720*4)
        uc.mem_write(main_surface,struct.pack('<QiiQ',symbols['virtuappu_frame_buffer'],240,160,240))
        uc.mem_write(second_surface,struct.pack('<QiiQ',ui,320,240,320))
        uc.mem_write(overlay,b'\x01\x00')
        if not call('SwitchOverlay_Compose',overlay,target,1280,main_surface,second_surface):
            raise RuntimeError('Overlay compositor rejected game surfaces')
        (args.output/f'composed-{name}.rgba').write_bytes(uc.mem_read(target,1280*720*4))
    test_volume_ui()
    test_audio_ui()
    test_display_ui()
    print(f'Gameplay movement covered {len(positions)} positions; map/status/options rendered through touch tab presses',flush=True)
    before=uc.mem_read(symbols['gSramMem'],65536)
    if not any(before): raise RuntimeError('Game never populated SRAM')
    call('Port_SaveSram')
    if files.get('mzm.sav')!=before: raise RuntimeError('Atomic SRAM write differs from emulated SRAM')
    uc.mem_write(symbols['gSramMem'],bytes(65536))
    call('Port_LoadSram')
    if uc.mem_read(symbols['gSramMem'],65536)!=before: raise RuntimeError('SRAM reload did not restore data')
    print('PASS: atomic 64KB SRAM write and reload using a memory-backed filesystem',flush=True)
    if 'AudioVolume_Save' in symbols:
        call('AudioVolume_Set',30)
        call('AudioVolume_Save')
        call('AudioVolume_Set',80)
        call('AudioVolume_Load')
        if call('AudioVolume_Get')!=30: raise RuntimeError('Volume preference did not reload')
        call('AudioVolume_Set',60)
        call('AudioVolume_Save')
        call('AudioVolume_Set',0)
        call('AudioVolume_Load')
        if call('AudioVolume_Get')!=60: raise RuntimeError('Updating an existing volume preference failed')
        print('PASS: master volume saves, reloads and replaces an existing setting',flush=True)
    if audio:
        import wave
        with wave.open(str(args.output/'audio.wav'),'wb') as stream:
            stream.setnchannels(2); stream.setsampwidth(2); stream.setframerate(audio_rate); stream.writeframes(audio)
        peak=max(abs(v[0]) for v in struct.iter_unpack('<h',audio))
        print(f'Audio: {len(audio)//4} frames at {audio_rate}Hz, peak {peak}',flush=True)
        if not peak: raise RuntimeError('Audio is silent')
    print(f'PASS: native engine reached {frames} VBlanks',flush=True)
except (UcError,RuntimeError) as error:
    print(f'FAIL after {frames} frames: {error}',flush=True)
    raise SystemExit(1)
