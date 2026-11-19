#!/usr/bin/env python3
"""Emulate the real presenter/compositor and forbid CPU reads of GPU memory."""
from pathlib import Path
import argparse
import subprocess
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_CODE
import unicorn.arm64_const as arm
import struct
from unicorn.arm64_const import UC_ARM64_REG_SP, UC_ARM64_REG_LR, UC_ARM64_REG_X0, UC_ARM64_REG_PC

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--devkitpro',type=Path,default=Path('C:/devkitPro'))
    parser.add_argument('--ounce-sdk',type=Path,required=True)
    args=parser.parse_args()
    platform=Path(__file__).resolve().parents[1]
    shared=platform.parent/'switch/source'
    build=platform/'build/presenter-test';build.mkdir(parents=True,exist_ok=True)
    suffix='.exe' if sys.platform=='win32' else ''
    cc=args.devkitpro/f'devkitA64/bin/aarch64-none-elf-gcc{suffix}'
    nm=args.devkitpro/f'devkitA64/bin/aarch64-none-elf-nm{suffix}'
    flags=['-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-ffunction-sections','-fdata-sections',
           '-Wall','-Wextra','-Werror','-D__SWITCH2__','-I'+str(shared),
           '-I'+str(args.ounce_sdk/'libounce/include'),'-I'+str(args.ounce_sdk/'portlibs/switch2/include')]
    obj=build/'presenter.o';overlay=build/'overlay.o'
    subprocess.run([str(cc),*flags,'-c',str(platform/'tests/presenter_test.c'),'-o',str(obj)],check=True)
    subprocess.run([str(cc),*flags,'-c',str(shared/'screen_overlay.c'),'-o',str(overlay)],check=True)
    undefined=subprocess.check_output([str(nm),'-u',str(obj)],text=True)
    stubs=['.text']
    for line in undefined.splitlines():
        name=line.split()[-1]
        if name.startswith('vk'):
            stubs += [f'.global {name}',f'.type {name}, %function',f'{name}:']
            if name=='vkAcquireNextImageKHR':stubs += ['str wzr, [x5]']
            stubs += ['mov w0, #0','ret']
    asm=build/'vulkan-stubs.S';asm.write_text('\n'.join(stubs)+'\n')
    elf_path=build/'presenter-test.elf'
    subprocess.run([str(cc),'-nostdlib','-static','-Wl,--gc-sections','-Wl,-e,overlay_test','-Wl,-Ttext=0x100000',
                    str(obj),str(overlay),str(asm),'-o',str(elf_path)],check=True)
    with elf_path.open('rb') as f:
        elf=ELFFile(f);uc=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
        segments=[s for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
        start=min(s['p_vaddr'] for s in segments)&~4095
        end=(max(s['p_vaddr']+s['p_memsz'] for s in segments)+4095)&~4095
        uc.mem_map(start,end-start)
        for segment in segments:uc.mem_write(segment['p_vaddr'],segment.data())
        symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        clock={'ticks':1000000,'slept':0,'submits':0}
        def counter(uc,address,size,register):
            uc.reg_write(getattr(arm,'UC_ARM64_REG_X'+str(register)),clock['ticks'])
            uc.reg_write(UC_ARM64_REG_PC,address+4)
        text=elf.get_section_by_name('.text')
        instructions=text.data()
        for offset in range(0,len(instructions)-3,4):
            word=struct.unpack_from('<I',instructions,offset)[0]
            if word&~31==0xd53be020:
                address=text['sh_addr']+offset
                uc.hook_add(UC_HOOK_CODE,counter,user_data=word&31,begin=address,end=address)
        def sleep(uc,address,size,unused):
            ns=uc.reg_read(UC_ARM64_REG_X0);ticks=(ns+31)//32
            clock['ticks']+=ticks;clock['slept']+=ticks*32
            uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_LR))
        uc.hook_add(UC_HOOK_CODE,sleep,begin=symbols['svcSleepThread'],end=symbols['svcSleepThread'])
        size=1280*720*4;upload=symbols['test_upload'];violations=[]
        phase={'composing':False}
        def set_phase(uc,access,address,size,value,unused):
            phase['composing']=bool(value)
        def gpu_read(uc,access,address,size,value,unused):
            violations.append('CPU read GPU upload memory');uc.emu_stop()
        def gpu_write(uc,access,address,size,value,unused):
            if phase['composing']:
                violations.append('compositor wrote GPU upload memory');uc.emu_stop()
        uc.hook_add(UC_HOOK_MEM_READ,gpu_read,begin=upload,end=upload+size-1)
        uc.hook_add(UC_HOOK_MEM_WRITE,gpu_write,begin=upload,end=upload+size-1)
        uc.hook_add(UC_HOOK_MEM_WRITE,set_phase,begin=symbols['cpu_phase'],end=symbols['cpu_phase']+3)
        def submitted(uc,address,size,unused):
            assert uc.mem_read(upload,1280*720*4)==uc.mem_read(symbols['test_canvas'],1280*720*4),'Submitted image changed'
            clock['submits']+=1
        uc.hook_add(UC_HOOK_CODE,submitted,begin=symbols['vkQueueSubmit'],end=symbols['vkQueueSubmit'])
        uc.mem_map(0x8000000,0x100000);uc.reg_write(UC_ARM64_REG_SP,0x8100000-16)
        stop=0x9000000;uc.mem_map(stop,4096);uc.reg_write(UC_ARM64_REG_LR,stop)
        uc.emu_start(symbols['overlay_test'],stop,timeout=120_000_000)
        assert not violations,violations
        assert uc.reg_read(UC_ARM64_REG_PC)==stop,'Test timed out'
        assert uc.reg_read(UC_ARM64_REG_X0)==0,f'Failed test case {uc.reg_read(UC_ARM64_REG_X0)}'
        assert uc.mem_read(upload,size)==uc.mem_read(symbols['test_canvas'],size),'Upload changed pixels'
        assert 20000000<=clock['slept']<=20000100,'Warm-up did not honor requested time'
        assert clock['submits']>=9,'Warm-up did not present frames'
    print('PASS: six layouts, popup dimming, filters, cached canvas isolation, exact upload, frame guards, coherent/noncoherent staging, timed startup warm-up and startup-service pumping.')

if __name__=='__main__':main()
