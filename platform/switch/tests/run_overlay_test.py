#!/usr/bin/env python3
"""Run the freestanding AArch64 compositor regression test using Unicorn.

Requires unicorn and pyelftools. Pass the path to overlay-test.elf.
"""
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_SP, UC_ARM64_REG_LR, UC_ARM64_REG_X0

with open(sys.argv[1], 'rb') as stream:
    elf = ELFFile(stream)
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    segments = [segment for segment in elf.iter_segments() if segment['p_type'] == 'PT_LOAD']
    start = min(segment['p_vaddr'] for segment in segments) & ~4095
    end = (max(segment['p_vaddr'] + segment['p_memsz'] for segment in segments) + 4095) & ~4095
    uc.mem_map(start, end - start)
    for segment in segments:
        uc.mem_write(segment['p_vaddr'], segment.data())
    symbols = elf.get_section_by_name('.symtab')
    entry = symbols.get_symbol_by_name('overlay_test')[0]['st_value']
    uc.mem_map(0x8000000, 0x100000)
    uc.reg_write(UC_ARM64_REG_SP, 0x8100000 - 16)
    stop = 0x9000000
    uc.mem_map(stop, 4096)
    uc.reg_write(UC_ARM64_REG_LR, stop)
    uc.emu_start(entry, stop, timeout=120_000_000)
    result = uc.reg_read(UC_ARM64_REG_X0)
    if result:
        raise SystemExit(f'FAIL: overlay test case {result}')
    print(f'PASS: {sys.argv[1]}')
