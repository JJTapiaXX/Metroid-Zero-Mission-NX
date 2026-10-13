#!/usr/bin/env python3
"""Build the native Metroid: Zero Mission NX Switch executable."""
from pathlib import Path
import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import subprocess
import time

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--devkitpro', type=Path, default=Path('C:/devkitPro'))
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    updater_build_time=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
    platform = Path(__file__).resolve().parent
    root = platform.parents[1]
    build = platform/'build/native'
    build.mkdir(parents=True, exist_ok=True)
    suffix = '.exe' if os.name == 'nt' else ''
    cc = args.devkitpro/f'devkitA64/bin/aarch64-none-elf-gcc{suffix}'
    files = list((root/'src').glob('*.c'))
    for directory in ['cutscenes','menus','sprites_ai','sram']:
        files += list((root/'src'/directory).glob('*.c'))
    files += [root/path for path in ['src/data/shortcut_pointers.c','src/data/music_track_data.c','src/data/audio.c','src/data/color_fading_data.c','src/data/cutscenes/cutscenes_data.c','src/data/in_game_cutscene_data.c','src/data/empty_datatypes.c','port/port_gba_symbols.c','port/port_rom.c','port/port_gba_mem.c','port/port_bios.c','port/port_debug_log.c','port/port_debug_files.c','port/port_constructor_init.c','port/ppu/src/virtuappu.c','port/ppu/src/mode1.c','port/port_psg_synth.c']]
    files += [p for p in (root/'port/generated').glob('*.c') if not p.stem.endswith(('_test','_selftest'))]
    files += list((platform/'source').glob('*.c')) + list((platform/'source/generated').glob('*.c'))
    files += list((platform/'source').glob('*.S'))
    files += list((root/'third_party/rcheevos/src/rcheevos').glob('*.c'))
    files += [root/'third_party/rcheevos/src'/name for name in ('rc_util.c','rc_compat.c','rhash/md5.c','rapi/rc_api_common.c','rapi/rc_api_user.c','rapi/rc_api_runtime.c')]
    architecture = ['-march=armv8-a+crc+crypto','-mtune=cortex-a57','-mtp=soft','-fPIE']
    common = ['-O2','-g','-std=gnu11','-ffunction-sections','-fdata-sections','-Wall','-DPORT_NATIVE','-DPLATFORM_SWITCH','-DMODE1_GBA_WIDTH=288',*architecture]
    includes = ['-I'+str(root/path) for path in ['port/generated/shadow','port/generated','include','port','port/ppu/include','.','platform/switch/source']]
    includes += ['-I'+str(args.devkitpro/'libnx/include')]
    includes += ['-I'+str(root/'third_party/rcheevos'/name) for name in ('include','src','src/rcheevos','src/rhash','src/rapi')]
    compatibility = ['-fcommon','-fshort-enums','-DREGION_EU','-DNON_MATCHING=1','-DMZM_3DS','-DPLATFORM_LINUX','-include','string.h','-Wno-implicit-function-declaration','-Wno-implicit-int','-Wno-int-conversion','-Wno-incompatible-pointer-types','-Wno-builtin-declaration-mismatch']
    header_hash = hashlib.sha256()
    for directory in [root/'include',root/'port',platform/'source']:
        for header in sorted(directory.rglob('*.h')):
            header_hash.update(header.read_bytes())
    # MZM_3DS selects existing runtime constructors. PLATFORM_LINUX disables
    # 3DS SDK callbacks; PLATFORM_SWITCH supplies the actual backend.
    def compile_one(path):
        rel = path.relative_to(root)
        obj = build/rel.with_suffix('.o')
        obj.parent.mkdir(parents=True, exist_ok=True)
        flags = common + includes
        if path.name == 'updater_switch.c':
            flags += ['-DNX_UPDATER_BUILD_TIME="'+updater_build_time+'"']
        if path.name in {'mode1.c','virtuappu.c','screen_overlay.c'}:
            flags[flags.index('-O2')]='-O3'
        if str(rel).startswith('platform') and path.suffix=='.c':
            flags += ['-Wextra','-Werror']
        if str(rel).startswith('third_party'):
            flags += ['-DRC_DISABLE_LUA']
        elif not str(rel).startswith(('platform','port/ppu','port\\ppu')):
            flags += compatibility
        elif path.name in {'game_switch.c','switch_ui.c','audio_native.c'}:
            flags += ['-DREGION_EU','-fshort-enums','-include','string.h']
        command = [str(cc),*flags,'-c',str(path),'-o',str(obj)]
        # Header content is part of the fingerprint: never silently reuse an
        # object after a ROM decoder or include/layout change.
        fingerprint = hashlib.sha256(json.dumps(command).encode()+path.read_bytes()+header_hash.digest()).hexdigest()
        stamp = obj.with_suffix('.stamp')
        if obj.exists() and stamp.exists() and stamp.read_text()==fingerprint:
            return obj, ''
        result = subprocess.run(command,capture_output=True,text=True)
        obj.with_suffix('.log').write_text(result.stderr)
        if result.returncode:
            return obj,result.stderr
        stamp.write_text(fingerprint)
        return obj,''
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results=list(pool.map(compile_one,files))
    errors=[(path,error) for path,error in results if error]
    if errors:
        for path,error in errors: print(path,error[:4000])
        raise SystemExit(f'{len(errors)} compilation failures')
    print(f'Compiled {len(results)} sources',flush=True)
    elf = build/'mzm-switch-bringup.elf'
    env = dict(os.environ,DEVKITPRO=args.devkitpro.as_posix())
    defs=[]
    response=build/'link.rsp'
    response.write_text('\n'.join('"'+str(path).replace('\\','/')+'"' for path,error in results))
    for line in (root/'asm/linker_symbols.s').read_text().splitlines():
        match=re.search(r'\.set\s+(\w+),\s*(\S+)',line)
        if match: defs.append('-Wl,--defsym,'+match[1]+'='+match[2])
    result=subprocess.run([str(cc),*architecture,'-specs='+str(args.devkitpro/'libnx/switch.specs'),'-Wl,--gc-sections',*defs,'@'+str(response),'-L'+str(args.devkitpro/'libnx/lib'),'-lnx','-lm','-o',str(elf)],env=env,capture_output=True,text=True)
    (build/'link.log').write_text(result.stderr)
    if result.returncode: raise SystemExit(result.stderr[-12000:])
    from tools.gen_native_metadata import main as patch_metadata
    patch_metadata(elf,root,args.devkitpro)
    nacp=build/'mzm-switch-bringup.nacp'
    subprocess.run([str(args.devkitpro/f'tools/bin/nacptool{suffix}'),'--create','Metroid: Zero Mission NX','JJTapia_XX','1.1',str(nacp)],check=True)
    nro=platform/'mzm-switch-bringup.nro'
    subprocess.run([str(args.devkitpro/f'tools/bin/elf2nro{suffix}'),str(elf),str(nro),'--nacp='+str(nacp),'--icon='+str(platform/'assets/icon.jpg')],check=True)
    print(f'Built {nro}')

if __name__=='__main__': main()
