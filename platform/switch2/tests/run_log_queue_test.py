#!/usr/bin/env python3
"""Check nonblocking diagnostics with a mocked SD writer and emulated AArch64."""
from pathlib import Path
import argparse,subprocess,sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_SP,UC_ARM64_REG_LR,UC_ARM64_REG_X0,UC_ARM64_REG_PC

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--ounce-sdk',type=Path,required=True)
    p.add_argument('--devkitpro',type=Path,default=Path('C:/devkitPro'))
    p.add_argument('--case',choices=['log','cache'],default='log')
    args=p.parse_args();platform=Path(__file__).resolve().parents[1]
    build=platform/f'build/{args.case}-queue-test';build.mkdir(parents=True,exist_ok=True)
    suffix='.exe' if sys.platform=='win32' else ''
    cc=args.devkitpro/f'devkitA64/bin/aarch64-none-elf-gcc{suffix}';elf_path=build/'log-queue-test.elf'
    subprocess.run([str(cc),'-O2','-ffreestanding','-fno-builtin','-fno-stack-protector',
        '-ffunction-sections','-fdata-sections','-nostdlib','-static','-Wall','-Wextra','-Werror',
        '-Wl,--gc-sections','-Wl,-e,overlay_test','-Wl,-Ttext=0x100000','-D__SWITCH2__',
        '-I'+str(args.ounce_sdk/'libounce/include'),'-I'+str(platform.parent/'switch/source'),
        str(platform/f'tests/{args.case}_queue_test.c'),'-o',str(elf_path)],check=True)
    with elf_path.open('rb') as f:
        elf=ELFFile(f);uc=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
        segments=[s for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
        start=min(s['p_vaddr'] for s in segments)&~4095
        end=(max(s['p_vaddr']+s['p_memsz'] for s in segments)+4095)&~4095
        uc.mem_map(start,end-start)
        for s in segments:uc.mem_write(s['p_vaddr'],s.data())
        entry=elf.get_section_by_name('.symtab').get_symbol_by_name('overlay_test')[0]['st_value']
        uc.mem_map(0x8000000,0x100000);uc.reg_write(UC_ARM64_REG_SP,0x8100000-16)
        stop=0x9000000;uc.mem_map(stop,4096);uc.reg_write(UC_ARM64_REG_LR,stop)
        uc.emu_start(entry,stop,timeout=10_000_000)
        assert uc.reg_read(UC_ARM64_REG_PC)==stop,'Timeout'
        assert uc.reg_read(UC_ARM64_REG_X0)==0,f'Failure {uc.reg_read(UC_ARM64_REG_X0)}'
    if args.case=='cache':print('PASS: achievement cache enqueue has zero SD I/O; immutable signed snapshots; coalescing; critical priority/eviction; producer during write; flush/drain; failure retry; shutdown.')
    else:print('PASS: zero producer SD calls, bounded saturation, drop reporting, no queue lock during I/O, concurrent producer, drain, wraparound, disabled fallback.')

if __name__=='__main__':main()
