#!/usr/bin/env python3
"""Generate GBA->AArch64 ROM layout schemas from compiler DWARF metadata.

No ROM contents are embedded. Both compilers inspect the same generated C
declarations, so field offsets, enum widths, nested arrays and padding are
measured rather than guessed. Requires pyelftools, devkitARM and devkitA64.
"""
from pathlib import Path
from contextlib import ExitStack
import argparse
import concurrent.futures
import json
import hashlib
import re
import subprocess
from elftools.elf.elffile import ELFFile

ALIASES = {'DW_TAG_const_type', 'DW_TAG_volatile_type', 'DW_TAG_typedef', 'DW_TAG_restrict_type'}

def unwrap(die):
    while die.tag in ALIASES:
        die = die.get_DIE_from_attribute('DW_AT_type')
    return die

def array_count(die):
    count = 1
    for child in die.iter_children():
        if child.tag != 'DW_TAG_subrange_type':
            continue
        if 'DW_AT_count' in child.attributes:
            count *= child.attributes['DW_AT_count'].value
        elif 'DW_AT_upper_bound' in child.attributes:
            count *= child.attributes['DW_AT_upper_bound'].value + 1
        else:
            raise ValueError('Array has no compile-time bound')
    return count

def size(die):
    die = unwrap(die)
    if die.tag == 'DW_TAG_array_type':
        return array_count(die) * size(die.get_DIE_from_attribute('DW_AT_type'))
    return die.attributes['DW_AT_byte_size'].value

def shape(a, b):
    a, b = unwrap(a), unwrap(b)
    if a.tag != b.tag:
        raise ValueError(f'Type mismatch: {a.tag} / {b.tag}')
    sa, sb = size(a), size(b)
    if a.tag == 'DW_TAG_pointer_type':
        if (sa, sb) != (4, 8):
            raise ValueError(f'Unexpected pointer widths {sa}/{sb}')
        return ('pointer', sa, sb, ())
    if a.tag == 'DW_TAG_array_type':
        na, nb = array_count(a), array_count(b)
        if na != nb:
            raise ValueError('Array bounds disagree')
        child = shape(a.get_DIE_from_attribute('DW_AT_type'), b.get_DIE_from_attribute('DW_AT_type'))
        if child[0] == 'copy':
            return ('copy', sa, sb, ())
        return ('array', sa, sb, ((na, 0, child),))
    if a.tag == 'DW_TAG_structure_type':
        ma = [c for c in a.iter_children() if c.tag == 'DW_TAG_member']
        mb = [c for c in b.iter_children() if c.tag == 'DW_TAG_member']
        if len(ma) != len(mb):
            raise ValueError('Member counts disagree')
        fields = []
        for x, y in zip(ma, mb):
            if x.attributes.get('DW_AT_name').value != y.attributes.get('DW_AT_name').value:
                raise ValueError('Member names disagree')
            if 'DW_AT_bit_size' in x.attributes:
                if x.attributes.get('DW_AT_data_bit_offset').value != y.attributes.get('DW_AT_data_bit_offset').value:
                    raise ValueError('Bitfield layout differs')
                bit_start = x.attributes['DW_AT_data_bit_offset'].value
                bit_end = bit_start + x.attributes['DW_AT_bit_size'].value
                byte_start = bit_start // 8
                length = (bit_end + 7) // 8 - byte_start
                fields.append((byte_start, byte_start, ('copy', length, length, ())))
                continue
            ox = x.attributes['DW_AT_data_member_location'].value
            oy = y.attributes['DW_AT_data_member_location'].value
            fields.append((ox, oy, shape(x.get_DIE_from_attribute('DW_AT_type'), y.get_DIE_from_attribute('DW_AT_type'))))
        if sa == sb and all(x == y and child[0] == 'copy' for x, y, child in fields):
            return ('copy', sa, sb, ())
        return ('record', sa, sb, tuple(fields))
    if a.tag == 'DW_TAG_union_type':
        # Pointer-bearing unions need a discriminator. Do not silently choose an arm.
        for child in a.iter_children():
            if child.tag == 'DW_TAG_member' and contains_pointer(child.get_DIE_from_attribute('DW_AT_type')):
                raise ValueError('Pointer-bearing union requires explicit decoding')
    if sa != sb:
        raise ValueError(f'Non-pointer scalar size differs: {sa}/{sb}')
    return ('copy', sa, sb, ())

def contains_pointer(die):
    die = unwrap(die)
    if die.tag == 'DW_TAG_pointer_type':
        return True
    if die.tag == 'DW_TAG_array_type':
        return contains_pointer(die.get_DIE_from_attribute('DW_AT_type'))
    if die.tag in {'DW_TAG_structure_type', 'DW_TAG_union_type'}:
        return any(contains_pointer(c.get_DIE_from_attribute('DW_AT_type')) for c in die.iter_children() if c.tag == 'DW_TAG_member')
    return False

def variables(elf):
    result = {}
    for cu in elf.get_dwarf_info().iter_CUs():
        for die in cu.iter_DIEs():
            name = die.attributes.get('DW_AT_name')
            if die.tag == 'DW_TAG_variable' and name and name.value.startswith(b'p_s'):
                result[name.value.decode()] = die.get_DIE_from_attribute('DW_AT_type')
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--devkitpro', type=Path, default=Path('C:/devkitPro'))
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = root / 'platform/switch/build/rom-layouts'
    output = root / 'platform/switch/source/generated'
    output.mkdir(parents=True, exist_ok=True)
    flags = ['-g', '-O0', '-std=gnu11', '-fshort-enums', '-DPORT_NATIVE', '-DREGION_EU',
             '-include', 'string.h', '-I'+str(root/'include'), '-I'+str(root/'port'), '-I'+str(root)]
    files = sorted(p for p in (root/'port/generated').glob('*_rom.c') if '_test' not in p.stem)
    compilers = {32: args.devkitpro/'devkitARM/bin/arm-none-eabi-gcc.exe', 64: args.devkitpro/'devkitA64/bin/aarch64-none-elf-gcc.exe'}
    if not compilers[32].exists():
        compilers = {bits: Path(str(path).removesuffix('.exe')) for bits, path in compilers.items()}
    header_digest=hashlib.sha256()
    for directory in [root/'include',root/'port']:
        for header in sorted(directory.rglob('*.h')): header_digest.update(header.read_bytes())
    def compile_one(task):
        bits, path = task
        obj = build/str(bits)/path.with_suffix('.o').name
        obj.parent.mkdir(parents=True, exist_ok=True)
        fingerprint=hashlib.sha256(path.read_bytes()+header_digest.digest()+json.dumps(flags).encode()).hexdigest()
        stamp=obj.with_suffix('.stamp')
        if obj.exists() and stamp.exists() and stamp.read_text()==fingerprint:
            return
        result = subprocess.run([str(compilers[bits]), *flags, '-c', str(path), '-o', str(obj)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f'{path.name} ({bits}-bit):\n{result.stderr}')
        stamp.write_text(fingerprint)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(compile_one, [(bits, path) for path in files for bits in (32,64)]))
    schemas, ids, roots, errors = [], {}, [], []
    def register(node):
        if node in ids:
            return ids[node]
        children = [(x, y, register(child)) for x, y, child in node[3]]
        index = len(schemas)
        ids[node] = index
        schemas.append((node[0], node[1], node[2], children))
        return index
    for path in files:
        with ExitStack() as stack:
            ea = ELFFile(stack.enter_context((build/'32'/path.with_suffix('.o').name).open('rb')))
            eb = ELFFile(stack.enter_context((build/'64'/path.with_suffix('.o').name).open('rb')))
            va, vb = variables(ea), variables(eb)
            region = None
            for line in path.read_text().splitlines():
                match = re.search(r'case PORT_ROM_REGION_(EU|US|JP)', line)
                if match: region = match[1]
                match = re.search(r'(p_s\w+)\s*=.*Port_ResolveRomData\((0x[0-9a-f]+)u\)', line)
                if not match or not region:
                    continue
                name, address = match.groups()
                try:
                    ta = unwrap(va[name]).get_DIE_from_attribute('DW_AT_type')
                    tb = unwrap(vb[name]).get_DIE_from_attribute('DW_AT_type')
                    node = shape(ta, tb)
                    if node[0] == 'copy':
                        continue
                    roots.append({'region':region, 'address':int(address,16), 'schema':register(node), 'name':name})
                except (ValueError, KeyError, AttributeError) as error:
                    errors.append(f'{path.name}/{name}/{region}: {error}')
    report = {'schemas':schemas, 'roots':roots, 'errors':errors}
    (build/'report.json').write_text(json.dumps(report, indent=2))
    if errors:
        print('\n'.join(errors[:30]))
        raise SystemExit(f'Refusing to emit incomplete schemas: {len(errors)} errors')
    lines = ['/* Generated by tools/gen_switch_rom_layouts.py. No ROM assets. */', '#include "../rom64.h"']
    fields, type_rows = [], []
    kinds = {'copy':'ROM64_COPY','pointer':'ROM64_POINTER','array':'ROM64_ARRAY','record':'ROM64_RECORD'}
    for kind, sa, sb, children in schemas:
        first = len(fields)
        fields.extend(children)
        type_rows.append(f'    {{{kinds[kind]}, {sa}, {sb}, {first}, {len(children)}}},')
    lines += ['const Rom64Field gRom64Fields[] = {']
    lines += [f'    {{{x}, {y}, {child}}},' for x,y,child in fields]
    lines += ['};', 'const Rom64Type gRom64Types[] = {', *type_rows, '};', 'const Rom64Root gRom64Roots[] = {']
    # Region ids match PortRomRegion in port_rom.h.
    regions = {'US':1,'EU':2,'JP':3}
    unique = {}
    for r in roots:
        key = (r['region'], r['address'])
        if key in unique and unique[key]['schema'] != r['schema']:
            raise ValueError(f'Conflicting converted layouts at {key}: {r["name"]}')
        unique[key] = r
    lines += [f'    {{{regions[r["region"]]}, 0x{r["address"]:08x}u, {r["schema"]}}}, /* {r["name"]} */' for r in sorted(unique.values(),key=lambda r:(regions[r['region']],r['address']))]
    lines += ['};', f'const size_t gRom64RootCount = {len(unique)};']
    (output/'rom64_tables.c').write_text('\n'.join(lines)+'\n')
    print(f'Generated {len(schemas)} layouts and {len(unique)} converted roots')

if __name__ == '__main__':
    main()
