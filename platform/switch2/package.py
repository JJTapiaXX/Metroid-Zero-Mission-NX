#!/usr/bin/env python3
"""Package an experimental Switch 2 build and its corresponding source."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import argparse
import hashlib
import json
import struct
import subprocess

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--git', default='git')
    parser.add_argument('--test-log', type=Path, help='Completed portable regression test log')
    parser.add_argument('--presenter-test-log',type=Path,help='Completed Vulkan presenter isolation test log')
    parser.add_argument('--log-queue-test-log',type=Path,help='Completed nonblocking logging regression test log')
    parser.add_argument('--cache-test-log',type=Path)
    parser.add_argument('--achievement-test-log',type=Path)
    parser.add_argument('--map-test-log',type=Path)
    parser.add_argument('--also-switch1',action='store_true')
    args=parser.parse_args()
    platform=Path(__file__).resolve().parent
    root=platform.parents[1]
    out=args.output.resolve(); out.mkdir(parents=True,exist_ok=True)
    nro=platform/'mzmnx-switch2.nro'; data=nro.read_bytes()
    assert data[0x10:0x14]==b'NRO0', 'Invalid NRO header'
    aset=struct.unpack_from('<I',data,0x18)[0]
    assert data[aset:aset+4]==b'ASET', 'Missing NRO asset section'
    offset,length=struct.unpack_from('<QQ',data,aset+24)
    nacp=data[aset+offset:aset+offset+length]
    assert nacp[:0x200].split(b'\0')[0]==b'Metroid: Zero Mission NX Switch 2'
    assert nacp[0x200:0x300].split(b'\0')[0]==b'JJTapia_XX'
    assert nacp[0x3060:0x3070].split(b'\0')[0]==b'1.4'
    icon_offset,icon_length=struct.unpack_from('<QQ',data,aset+8)
    assert data[aset+icon_offset:aset+icon_offset+icon_length]==(root/'platform/switch/assets/icon.jpg').read_bytes()
    manifest={'target':'Switch 2 / Kaihen / OunceVK','version':'1.4 experimental',
        'hardware_tested':False,'save_state_build_check':True,
        'renderer':'Software RGBA compositor, Vulkan upload/blit presentation',
        'sdk_archive':'ounce-devkit-20261117.tar.xz',
        'nro_sha256':hashlib.sha256(data).hexdigest(),
        'limitations':['Requires Switch 2 hardware validation','Updater uses only nx-switch2-v releases and mzmnx-switch2.nro']}
    if args.test_log:
        test_log=args.test_log.read_text(encoding='utf-8')
        tests=['overlay','input','audio_output','audio_volume','audio_filters','network_socket']
        for test in tests:
            assert any('PASS:' in line and f'{test}-test.elf' in line for line in test_log.splitlines()), test
        manifest['portable_tests_passed']=tests
    if args.presenter_test_log:
        assert 'PASS: six layouts' in args.presenter_test_log.read_text(encoding='utf-8')
        manifest['presenter_test']='All six layouts; no CPU reads of upload memory; cached canvas; exact upload; frame guards; coherent/noncoherent staging; timed warm-up and startup service pumping (mock Vulkan, emulated AArch64)'
        manifest['performance_change']='Compose in cached CPU memory and upload once; R3/popup timing diagnostics enabled'
    if args.log_queue_test_log:
        assert 'PASS: zero producer SD calls' in args.log_queue_test_log.read_text(encoding='utf-8')
        manifest['logging_test']='Bounded queue; zero producer SD I/O; saturation/drop reporting; writer releases queue lock before SD I/O; producer during write; drain/wraparound/fallback (emulated AArch64)'
        manifest['save_stall_change']='Background runtime log writer; save-write duration and full-frame-gap diagnostics; atomic save/backup behavior retained'
    for supplied,key,needle in [(args.cache_test_log,'cache_queue_test','PASS: achievement cache enqueue'),
        (args.achievement_test_log,'achievement_engine_test','PASS: real unlock evaluation'),
        (args.map_test_log,'map_ui_test','PASS: native area/world maps')]:
        if supplied:
            assert needle in supplied.read_text(encoding='utf-8'),key
            manifest[key]='passed; emulated AArch64 / mocked services; hardware confirmation pending'
    manifest['startup_warmup_ms']=2000
    manifest['second_screen_credit']='tinaut1986: original 3DS UI reference; connected world atlas is a new Switch integration'
    (out/'VALIDATION.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    binary=out/'Metroid-Zero-Mission-NX-Switch2-experimental.zip'
    with ZipFile(binary,'w',ZIP_DEFLATED) as z:
        z.writestr('switch/mzm-switch2/mzmnx-switch2.nro',data)
        z.write(platform/'README.md','README.md')
        z.write(platform/'THIRD_PARTY_NOTICES.md','SWITCH2-THIRD-PARTY-NOTICES.md')
        for name in ['LICENSE','CREDITS.md','THIRD-PARTY-LICENSES.md']:
            z.write(root/name,name)
        for path in sorted((platform/'licenses').glob('*')):
            z.write(path,'licenses/'+path.name)
        z.write(out/'VALIDATION.json','VALIDATION.json')
        for note in ['switch-unreleased.md','switch2-unreleased.md']:
            z.write(root/'docs/release-notes'/note,'release-notes/'+note)
        if (root/'docs/images/unreleased').exists():
            for preview in sorted((root/'docs/images/unreleased').glob('*')):z.write(preview,'previews/'+preview.name)
    archives=[binary]
    if args.also_switch1:
        original=(root/'platform/switch/mzm-switch-bringup.nro').read_bytes()
        assert original[0x10:0x14]==b'NRO0'
        base=struct.unpack_from('<I',original,0x18)[0]
        position,length=struct.unpack_from('<QQ',original,base+24)
        info=original[base+position:base+position+length]
        assert info[:512].split(b'\0')[0]==b'Metroid: Zero Mission NX'
        assert info[512:768].split(b'\0')[0]==b'JJTapia_XX'
        switch1=out/'Metroid-Zero-Mission-NX-Switch1-test.zip'
        with ZipFile(switch1,'w',ZIP_DEFLATED) as z:
            z.writestr('switch/mzm/mzmnx.nro',original)
            z.write(root/'platform/switch/README.md','README.md')
            for name in ['LICENSE','CREDITS.md','THIRD-PARTY-LICENSES.md']:z.write(root/name,name)
            z.write(root/'docs/release-notes/switch-unreleased.md','NEW-SECOND-SCREEN.md')
            if (root/'docs/images/unreleased').exists():
                for preview in sorted((root/'docs/images/unreleased').glob('*')):z.write(preview,'previews/'+preview.name)
        archives.append(switch1)
    files=subprocess.check_output([args.git,'-C',str(root),'ls-files','-z'],encoding='utf-8').split('\0')
    files += [p.relative_to(root).as_posix() for p in platform.rglob('*')
              if p.is_file() and not any(x in p.relative_to(platform).parts for x in ['build','__pycache__'])]
    files += ['platform/switch/tests/freestanding_memory.c']
    files += [p.relative_to(root).as_posix() for p in (root/'docs/release-notes').glob('*unreleased.md')]
    if (root/'docs/images/unreleased').exists():files += [p.relative_to(root).as_posix() for p in (root/'docs/images/unreleased').glob('*')]
    source=out/'Metroid-Zero-Mission-NX-Switch2-source.zip'
    with ZipFile(source,'w',ZIP_DEFLATED) as z:
        for name in sorted(set(files)):
            if not name: continue
            p=root/name
            if p.suffix.lower() in {'.gba','.sav','.srm','.elf','.nro','.log','.pyc','.bin'}: continue
            if p.name in {'ra-account.cfg','crash-last.txt'}: continue
            if p.is_file():z.write(p,'Metroid-Zero-Mission-NX-Switch2/'+name)
    archives.append(source)
    for archive in archives:
        with ZipFile(archive) as z:
            assert z.testzip() is None
            assert not any(n.lower().endswith(('.gba','.sav','.srm')) for n in z.namelist())
    (out/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in archives),encoding='utf-8')
    print('Verified NRO title, author, version, icon, archive integrity, and ROM/save exclusions.')
    for archive in archives:print(archive)

if __name__=='__main__': main()
