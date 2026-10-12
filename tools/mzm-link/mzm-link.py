#!/usr/bin/env python3
"""Drive the running game from the PC (debug builds, docs/3ds-debug-tools.md "Remote control").

Talks to the game's own FTP server (DEBUG -> HERRAMIENTAS -> FTP SERVER, port 5000) with the
SITE commands of platform/3ds/source/debug_link_3ds.h. Standard library only.

    mzm-link.py [--ip IP] COMMAND [ARGS]

    wait [SECONDS]            until the game answers (after an install or boot); default 120
    state                     frame, game mode, area, room, Samus's position, energy, ammo
    keys BUTTONS [FRAMES]     hold A+B+LEFT... (3DS names) for N logic frames, default 1
    press BUTTONS [FRAMES]    keys, then wait until those frames have passed
    touch X Y                 tap the bottom screen (320x240)
    frames N                  wait until the game has run N more logic frames
    sleep SECONDS             wait on the PC
    savestate [N]             save a new state, or over number N
    loadstate [N]             load save state N (1 = newest, the default)
    delstate N                delete save state N
    states                    the save states: number=area/room
    dump                      take a screen dump set (DEBUG -> HERRAMIENTAS -> SCREEN DUMP)
    switch NAME               flip RENDERER, FRAME SKIP, DEPTH TINT, OLD 3DS PROFILE or LOG BUFFERING
    shot [DIR]                screenshots of both screens as PNG, default /tmp/mzm-shot
    peek WHAT LENGTH          hex bytes; WHAT is a hex address, a symbol of the ELF, SYMBOL+HEX,
                              ewram:ADDR or iwram:ADDR (GBA addresses: ewram:03000000 is gEwram+0)
    poke WHAT HEXBYTES        write bytes there
    log [DIR]                 fetch the newest SD log, default /tmp/mzm-debug
    run FILE                  one command per line ('#' comments), stops at the first error

The IP defaults to .3ds_ftp_ip in the repository (the build assistant's last).
Symbols come from the ELF in build/ through arm-none-eabi-nm.
"""
import ftplib
import io
import os
import shlex
import struct
import subprocess
import sys
import time
import zlib

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
ELF = os.path.join(REPO, 'platform', '3ds', 'mzm-3ds.elf')
NM = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm'
DATA = '/3ds/Metroid Zero Mission 3DS'
PORT = 5000


def credentials():
    """The in-game FTP server's login, from platform/3ds/source/ftp_credentials_3ds.h."""
    import re
    text = open(os.path.join(REPO, 'platform', '3ds', 'source', 'ftp_credentials_3ds.h')).read()
    return tuple(re.search(r'#define FTP_%s "([^"]*)"' % k, text).group(1) for k in ('USER', 'PASS'))


class LinkError(Exception):
    pass


class Link:
    def __init__(self, ip):
        self.ip = ip
        self.ftp = None
        self.symbols = None

    def connect(self, timeout=10):
        if self.ftp is None:
            ftp = ftplib.FTP()
            ftp.connect(self.ip, PORT, timeout=timeout)
            ftp.login(*credentials())
            self.ftp = ftp
        return self.ftp

    def close(self):
        if self.ftp is not None:
            try:
                self.ftp.quit()
            except Exception:
                pass
            self.ftp = None

    def site(self, text):
        try:
            # A save or load can keep the game busy a few seconds.
            self.connect().sock.settimeout(40)
            reply = self.connect().sendcmd('SITE ' + text)
        except ftplib.error_reply as e:
            reply = str(e)
        except ftplib.all_errors as e:
            self.ftp = None
            raise LinkError(str(e))
        if not reply.startswith('2'):
            raise LinkError(reply)
        return reply[4:]

    def state(self):
        return dict(kv.split('=', 1) for kv in self.site('STATE').split())

    def frame(self):
        return int(self.state()['frame'])

    def wait_frames(self, n, timeout=60):
        target = self.frame() + n
        end = time.time() + timeout
        while self.frame() < target:
            if time.time() > end:
                raise LinkError('the game stopped advancing')
            time.sleep(0.05)

    def retrieve(self, remote, local):
        with open(local, 'wb') as f:
            self.connect().retrbinary('RETR ' + remote, f.write)

    def elf(self):
        if not os.path.isfile(ELF):
            raise LinkError('no %s for symbols (build first)' % ELF)
        return ELF

    def symbol(self, name):
        if self.symbols is None:
            elf = self.elf()
            out = subprocess.run([NM, '-C', elf], capture_output=True, text=True, check=True).stdout
            self.symbols = {}
            for line in out.splitlines():
                parts = line.split()
                if len(parts) >= 3:
                    self.symbols[parts[2]] = int(parts[0], 16)
        if name not in self.symbols:
            raise LinkError('no symbol %s in %s' % (name, self.elf()))
        return self.symbols[name]

    def address(self, what):
        low = what.lower()
        if low.startswith('ewram:'):
            return self.symbol('gEwram') + (int(what[6:], 16) & 0x3FFFF)
        if low.startswith('iwram:'):
            return self.symbol('gIwram') + (int(what[6:], 16) & 0x7FFF)
        try:
            return int(what, 16)
        except ValueError:
            pass
        name, _, offset = what.partition('+')
        return self.symbol(name) + (int(offset, 16) if offset else 0)


def bmp_to_png(data):
    """24-bit bottom-up BMP (what the game writes) to PNG bytes."""
    offset, = struct.unpack_from('<I', data, 10)
    width, height = struct.unpack_from('<ii', data, 18)
    stride = (width * 3 + 3) & ~3
    raw = bytearray()
    for y in range(height):
        row = data[offset + (height - 1 - y) * stride:][:width * 3]
        raw.append(0)
        for x in range(width):
            b, g, r = row[x * 3:x * 3 + 3]
            raw += bytes((r, g, b))

    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


def run(link, argv):
    cmd, args = argv[0], argv[1:]
    if cmd == 'wait':
        end = time.time() + (float(args[0]) if args else 120)
        while True:
            try:
                link.close()
                link.connect(timeout=3)
                print(link.site('STATE'))
                return
            except (LinkError, EOFError) + ftplib.all_errors:
                if time.time() > end:
                    raise LinkError('no answer from %s:%d' % (link.ip, PORT))
                time.sleep(2)
    if cmd == 'state':
        print(link.site('STATE'))
    elif cmd in ('keys', 'press'):
        frames = int(args[1]) if len(args) > 1 else 1
        print(link.site('KEYS %s %d' % (args[0], frames)))
        if cmd == 'press':
            link.wait_frames(frames + 1)
    elif cmd == 'touch':
        print(link.site('TOUCH %d %d' % (int(args[0]), int(args[1]))))
    elif cmd == 'frames':
        link.wait_frames(int(args[0]))
    elif cmd == 'sleep':
        time.sleep(float(args[0]))
    elif cmd in ('savestate', 'loadstate', 'delstate'):
        print(link.site((cmd.upper() + ' ' + ' '.join(args)).strip()))
    elif cmd == 'states':
        print(link.site('STATES'))
    elif cmd == 'dump':
        print(link.site('DUMP'))
    elif cmd == 'switch':
        print(link.site('SWITCH ' + ' '.join(args)))
    elif cmd == 'shot':
        out = args[0] if args else '/tmp/mzm-shot'
        os.makedirs(out, exist_ok=True)
        link.site('SHOT')
        for screen in ('top', 'bottom'):
            buf = io.BytesIO()
            link.connect().retrbinary('RETR %s/debug/shot-%s.bmp' % (DATA, screen), buf.write)
            path = os.path.join(out, screen + '.png')
            with open(path, 'wb') as f:
                f.write(bmp_to_png(buf.getvalue()))
            print(path)
    elif cmd == 'peek':
        address, length = link.address(args[0]), int(args[1], 0)
        data = ''
        while len(data) < length * 2:
            n = min(256, length - len(data) // 2)
            data += link.site('PEEK %x %d' % (address + len(data) // 2, n))
        print(data)
    elif cmd == 'poke':
        print(link.site('POKE %x %s' % (link.address(args[0]), args[1])))
    elif cmd == 'log':
        out = args[0] if args else '/tmp/mzm-debug'
        os.makedirs(out, exist_ok=True)
        names = sorted(n for n in link.connect().nlst(DATA + '/debug') if 'mzm-debug' in n)
        if not names:
            raise LinkError('no log on the SD card')
        name = os.path.basename(names[-1])
        path = os.path.join(out, name)
        link.retrieve('%s/debug/%s' % (DATA, name), path)
        print(path)
    elif cmd == 'run':
        with open(args[0]) as f:
            for line in f:
                line = line.split('#', 1)[0].strip()
                if line:
                    print('>', line)
                    run(link, shlex.split(line))
    else:
        raise LinkError('unknown command %s (see --help)' % cmd)


def main():
    argv = sys.argv[1:]
    if not argv or argv[0] in ('-h', '--help'):
        print(__doc__)
        return 0
    ip = None
    if argv[0] == '--ip':
        ip, argv = argv[1], argv[2:]
    if ip is None:
        try:
            with open(os.path.join(REPO, '.3ds_ftp_ip')) as f:
                ip = f.read().strip()
        except OSError:
            print('no --ip and no .3ds_ftp_ip', file=sys.stderr)
            return 2
    link = Link(ip)
    try:
        run(link, argv)
    except LinkError as e:
        print('mzm-link: %s' % e, file=sys.stderr)
        return 1
    finally:
        link.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
