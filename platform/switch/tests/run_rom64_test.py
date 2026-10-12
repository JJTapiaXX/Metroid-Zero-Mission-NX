#!/usr/bin/env python3
"""Validate converted native structures against a user's USA ROM in Unicorn."""
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_SP, UC_ARM64_REG_LR, UC_ARM64_REG_X0, UC_ARM64_REG_X1
with open(sys.argv[2], 'rb') as stream:
    rom = stream.read()
if rom[0xac:0xb0] != b'BMXE':
    raise SystemExit('This regression test expects the USA ROM')
with open(sys.argv[1], 'rb') as stream:
    elf = ELFFile(stream)
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    segments = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
    start = min(s['p_vaddr'] for s in segments) & ~4095
    end = (max(s['p_vaddr'] + s['p_memsz'] for s in segments) + 4095) & ~4095
    uc.mem_map(start, end - start)
    for s in segments: uc.mem_write(s['p_vaddr'], s.data())
    # Both code and the ROM buffer are placed above 4GB, as on a 64-bit console.
    rom_address = 0x7200000000
    uc.mem_map(rom_address, (len(rom) + 4095) & ~4095)
    uc.mem_write(rom_address, rom)
    uc.mem_map(0x7300000000, 0x100000)
    uc.reg_write(UC_ARM64_REG_SP, 0x7300100000 - 16)
    stop = 0x7400000000
    uc.mem_map(stop, 4096)
    uc.reg_write(UC_ARM64_REG_LR, stop)
    uc.reg_write(UC_ARM64_REG_X0, rom_address)
    uc.reg_write(UC_ARM64_REG_X1, len(rom))
    entry = elf.get_section_by_name('.symtab').get_symbol_by_name('rom64_test')[0]['st_value']
    uc.emu_start(entry, stop, timeout=60_000_000)
    result = uc.reg_read(UC_ARM64_REG_X0)
    if result: raise SystemExit(f'FAIL: ROM64 test case {result}')
    print('PASS: all converted USA roots load; sound entries, tilesets and room records match raw ROM; pointers work above 4GB')
