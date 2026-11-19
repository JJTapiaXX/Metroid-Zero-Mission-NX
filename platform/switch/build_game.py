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
    parser.add_argument('--target', choices=('switch', 'switch2'), default='switch')
    parser.add_argument('--ounce-sdk', type=Path, help='Root containing libounce and portlibs/switch2 (defaults to DEVKITPRO)')
    parser.add_argument('--allow-state-build-mismatch', action='store_true', help='Test build: allow loading states from other builds')
    args = parser.parse_args()
    updater_build_time=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
    platform = Path(__file__).resolve().parent
    root = platform.parents[1]
    switch2 = args.target == 'switch2'
    sdk = (args.ounce_sdk or args.devkitpro).resolve() if switch2 else args.devkitpro.resolve()
    libnx = sdk/('libounce' if switch2 else 'libnx')
    if not (libnx/'switch.specs').is_file():
        parser.error(f'Missing {libnx}/switch.specs; install ounce-devkit for --target switch2')
    output_platform = root/'platform/switch2' if switch2 else platform
    build = output_platform/'build/native'
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
    if switch2:
        files = [p for p in files if p.name != 'updater_switch.c']
        files += list((output_platform/'source').glob('*.c'))
    files += list((root/'third_party/rcheevos/src/rcheevos').glob('*.c'))
    files += [root/'third_party/rcheevos/src'/name for name in ('rc_util.c','rc_compat.c','rhash/md5.c','rapi/rc_api_common.c','rapi/rc_api_user.c','rapi/rc_api_runtime.c')]
    architecture = ['-march=armv8-a+crc+crypto','-mtune=cortex-a57','-mtp=soft','-fPIE']
    if switch2:
        cpu = (libnx/'switch2_cpu.mk').read_text()
        match = re.search(r'^SWITCH2_CPU\s*:=\s*(.+)$', cpu, re.M)
        if not match: parser.error('Cannot read SWITCH2_CPU from ounce-devkit')
        architecture = [*match[1].split(), '-mtp=soft', '-fPIE']
    common = ['-O2','-g','-std=gnu11','-ffunction-sections','-fdata-sections','-Wall','-DPORT_NATIVE','-DPLATFORM_SWITCH','-DMODE1_GBA_WIDTH=320',*architecture]
    includes = ['-I'+str(root/path) for path in ['port/generated/shadow','port/generated','include','port','port/ppu/include','.','platform/switch/source']]
    includes += ['-I'+str(libnx/'include')]
    if switch2:
        common += ['-D__SWITCH__', '-D__SWITCH2__', '-DPLATFORM_SWITCH2',
                   '-DMZM_DATA_PATH="sdmc:/switch/mzm-switch2"',
                   '-DMZM_CRASH_PATH="/switch/mzm-switch2/logs/crash-last.txt"']
        includes += ['-I'+str(output_platform/'source'), '-I'+str(sdk/'portlibs/switch2/include')]
    includes += ['-I'+str(root/'third_party/rcheevos'/name) for name in ('include','src','src/rcheevos','src/rhash','src/rapi')]
    compatibility = ['-fcommon','-fshort-enums','-DREGION_EU','-DNON_MATCHING=1','-DMZM_3DS','-DPLATFORM_LINUX','-include','string.h','-Wno-implicit-function-declaration','-Wno-implicit-int','-Wno-int-conversion','-Wno-incompatible-pointer-types','-Wno-builtin-declaration-mismatch']
    header_hash = hashlib.sha256()
    for directory in [root/'include',root/'port',platform/'source',libnx/'include',*([output_platform/'source'] if switch2 else [])]:
        for header in sorted(directory.rglob('*.h')):
            header_hash.update(header.read_bytes())
    # MZM_3DS selects existing runtime constructors. PLATFORM_LINUX disables
    # 3DS SDK callbacks; PLATFORM_SWITCH supplies the actual backend.
    def compile_one(path):
        rel = path.relative_to(root)
        obj = build/rel.with_suffix('.o')
        obj.parent.mkdir(parents=True, exist_ok=True)
        flags = common + includes
        if path.name == 'save_states.c' and args.allow_state_build_mismatch:
            flags += ['-DNX_ALLOW_STATE_BUILD_MISMATCH']
        if path.name in {'updater_switch.c','updater_ounce.c'}:
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
    elf = build/('mzmnx-switch2.elf' if switch2 else 'mzm-switch-bringup.elf')
    env = dict(os.environ,DEVKITPRO=sdk.as_posix())
    defs=[]
    response=build/'link.rsp'
    response.write_text('\n'.join('"'+str(path).replace('\\','/')+'"' for path,error in results))
    for line in (root/'asm/linker_symbols.s').read_text().splitlines():
        match=re.search(r'\.set\s+(\w+),\s*(\S+)',line)
        if match: defs.append('-Wl,--defsym,'+match[1]+'='+match[2])
    libraries = ['-L'+str(sdk/'portlibs/switch2/lib'), '-lvulkan'] if switch2 else []
    result=subprocess.run([str(cc),*architecture,'-specs='+str(libnx/'switch.specs'),'-Wl,--gc-sections',*defs,'@'+str(response),*libraries,'-L'+str(libnx/'lib'),'-lnx','-lm','-o',str(elf)],env=env,capture_output=True,text=True)
    (build/'link.log').write_text(result.stderr)
    if result.returncode: raise SystemExit(result.stderr[-12000:])
    from tools.gen_native_metadata import main as patch_metadata
    patch_metadata(elf,root,args.devkitpro)
    nacp=build/'mzm-switch-bringup.nacp'
    title = 'Metroid: Zero Mission NX Switch 2' if switch2 else 'Metroid: Zero Mission NX'
    subprocess.run([str(args.devkitpro/f'tools/bin/nacptool{suffix}'),'--create',title,'JJTapia_XX','1.4',str(nacp)],check=True)
    nro=output_platform/('mzmnx-switch2.nro' if switch2 else 'mzm-switch-bringup.nro')
    subprocess.run([str(args.devkitpro/f'tools/bin/elf2nro{suffix}'),str(elf),str(nro),'--nacp='+str(nacp),'--icon='+str(platform/'assets/icon.jpg')],check=True)
    print(f'Built {nro}')

if __name__=='__main__': main()
