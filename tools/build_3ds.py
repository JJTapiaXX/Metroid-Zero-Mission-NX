#!/usr/bin/env python3
"""Script de compilación y despliegue por FTP para Metroid Zero Mission 3DS.

Soporta ejecución no interactiva por parámetros y menú interactivo navegable
con flechas de dirección.
"""

import argparse
import datetime
import os
import re
import shutil
import socket
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

# Force UTF-8 on stdout/stderr. This script prints non-ASCII status glyphs
# (checkmarks, arrows, box drawing); on a legacy Windows console the default
# cp1252 codec raises UnicodeEncodeError on those, which aborts the script
# with a traceback *after* the build has already succeeded (or failed),
# hiding the real result. errors="replace" keeps it alive even on consoles
# that still can't render the glyph.
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

# Rutas base
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
PLATFORM_3DS = os.path.join(ROOT, "platform", "3ds")
IP_HISTORY_FILE = os.path.join(PLATFORM_3DS, ".3ds_ftp_ip")

# Códigos ANSI para colores y formato
RESET = "\033[0m"
BOLD = "\033[1m"
DIM = "\033[2m"
CYAN = "\033[36m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
RED = "\033[31m"
MAGENTA = "\033[35m"
BG_BLUE = "\033[44m"
BG_CYAN = "\033[46m"
WHITE = "\033[37m"
CLEAR_LINE = "\033[2K"
CURSOR_UP = "\033[F"
HIDE_CURSOR = "\033[?25l"
SHOW_CURSOR = "\033[?25h"


def supports_color():
    """Comprueba si la consola soporta colores ANSI."""
    if not sys.stdout.isatty():
        return False
    if os.name == "nt":
        # Windows 10+ soporta ANSI si VT100 está activado
        return True
    return os.environ.get("TERM") != "dumb"


class RawTerminal:
    """Manejo multiplataforma de lectura de teclado en tiempo real."""

    def __enter__(self):
        self.old_settings = None
        if os.name != "nt" and sys.stdin.isatty():
            import termios
            import tty
            self.fd = sys.stdin.fileno()
            self.old_settings = termios.tcgetattr(self.fd)
            # setcbreak desactiva el buffer de línea y el eco pero conserva la
            # traducción de saltos de línea (\n -> \r\n), evitando el efecto escalera
            tty.setcbreak(self.fd)
        sys.stdout.write(HIDE_CURSOR)
        sys.stdout.flush()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        sys.stdout.write(SHOW_CURSOR)
        sys.stdout.flush()
        if os.name != "nt" and self.old_settings is not None:
            import termios
            termios.tcsetattr(self.fd, termios.TCSADRAIN, self.old_settings)

    def get_key(self):
        """Lee una pulsación de tecla y la normaliza."""
        if os.name == "nt":
            import msvcrt
            ch = msvcrt.getch()
            if ch in (b"\x00", b"\xe0"):
                ch2 = msvcrt.getch()
                if ch2 == b"H":
                    return "UP"
                elif ch2 == b"P":
                    return "DOWN"
                elif ch2 == b"K":
                    return "LEFT"
                elif ch2 == b"M":
                    return "RIGHT"
                return "OTHER"
            elif ch in (b"\r", b"\n"):
                return "ENTER"
            elif ch == b"\x1b":
                return "ESC"
            elif ch == b"\x03":
                raise KeyboardInterrupt
            elif ch in (b"q", b"Q"):
                return "QUIT"
            elif ch in (b"w", b"W", b"k", b"K"):
                return "UP"
            elif ch in (b"s", b"S", b"j", b"J"):
                return "DOWN"
            elif ch == b" ":
                return "SPACE"
            return ch.decode("utf-8", errors="ignore")
        else:
            import select
            data = os.read(self.fd, 32)
            if not data:
                return "OTHER"
            if data in (b"\x1b[A", b"\x1bOA"):
                return "UP"
            elif data in (b"\x1b[B", b"\x1bOB"):
                return "DOWN"
            elif data in (b"\x1b[C", b"\x1bOC"):
                return "RIGHT"
            elif data in (b"\x1b[D", b"\x1bOD"):
                return "LEFT"
            elif data.startswith(b"\x1b[") or data.startswith(b"\x1bO"):
                if data.endswith(b"A"):
                    return "UP"
                elif data.endswith(b"B"):
                    return "DOWN"
                elif data.endswith(b"C"):
                    return "RIGHT"
                elif data.endswith(b"D"):
                    return "LEFT"
                return "OTHER"
            elif data == b"\x1b":
                # Si llega solo ESC, esperar brevemente por si es el prefijo de una secuencia lenta
                r, _, _ = select.select([self.fd], [], [], 0.05)
                if r:
                    more = os.read(self.fd, 31)
                    full = data + more
                    if full.endswith(b"A"):
                        return "UP"
                    elif full.endswith(b"B"):
                        return "DOWN"
                    elif full.endswith(b"C"):
                        return "RIGHT"
                    elif full.endswith(b"D"):
                        return "LEFT"
                    return "OTHER"
                return "ESC"
            elif data in (b"\r", b"\n"):
                return "ENTER"
            elif data == b"\x03":
                raise KeyboardInterrupt
            elif data in (b"q", b"Q"):
                return "QUIT"
            elif data in (b"w", b"W", b"k", b"K"):
                return "UP"
            elif data in (b"s", b"S", b"j", b"J"):
                return "DOWN"
            elif data == b" ":
                return "SPACE"
            return data.decode("utf-8", errors="ignore")


def clear_screen():
    if os.name == "nt":
        os.system("cls")
    else:
        sys.stdout.write("\033[2J\033[H")
        sys.stdout.flush()


HOTKEY_LABELS = {"r": "reintentar"}


def hotkeys_help(letter):
    """Human wording for a hotkey in the menu help line."""
    return HOTKEY_LABELS.get(letter.lower(), "la acción")


def interactive_select(title, options, default_index=0, subtitle=None,
                       hotkeys=None):
    """Muestra un menú interactivo navegable con flechas.

    `hotkeys` maps a single letter to a sentinel string returned as-is when
    that key is pressed, so a caller can offer an action (retrying a scan,
    say) without spending a list entry on it. Matching is case-insensitive;
    the keys are listed in the help line under the menu."""
    current = default_index
    num_options = len(options)

    with RawTerminal() as term:
        while True:
            clear_screen()
            print(f"{BOLD}{CYAN}=================================================={RESET}")
            print(f"{BOLD}{WHITE} Metroid Zero Mission 3DS - Asistente de Build{RESET}")
            print(f"{BOLD}{CYAN}=================================================={RESET}\n")

            print(f"{BOLD}{title}{RESET}")
            if subtitle:
                print(f"{DIM}{subtitle}{RESET}")
            print()

            for i, opt in enumerate(options):
                if i == current:
                    label, desc = opt if isinstance(opt, tuple) else (opt, "")
                    print(f"  {BOLD}{GREEN}➔ [X] {label}{RESET}")
                    if desc:
                        print(f"        {DIM}{desc}{RESET}")
                else:
                    label, desc = opt if isinstance(opt, tuple) else (opt, "")
                    print(f"    [ ] {label}")
                    if desc:
                        print(f"        {DIM}{desc}{RESET}")

            help_line = "Usa las flechas ↑ / ↓ o W/S para moverte, Enter para elegir"
            for letter in (hotkeys or {}):
                help_line += f", {letter.upper()} para {hotkeys_help(letter)}"
            help_line += ", Q para salir"
            print(f"\n{DIM}({help_line}){RESET}")

            key = term.get_key()
            if hotkeys and len(key) == 1 and key.lower() in hotkeys:
                return hotkeys[key.lower()]
            if key == "UP":
                current = (current - 1) % num_options
            elif key == "DOWN":
                current = (current + 1) % num_options
            elif key == "ENTER" or key == "SPACE":
                return current
            elif key in ("ESC", "QUIT"):
                print(f"\n{YELLOW}Operación cancelada.{RESET}")
                sys.exit(0)


def prompt_text(prompt_label, default_value=""):
    """Solicita una entrada de texto interactiva."""
    clear_screen()
    print(f"{BOLD}{CYAN}=================================================={RESET}")
    print(f"{BOLD}{WHITE} Metroid Zero Mission 3DS - Asistente de Build{RESET}")
    print(f"{BOLD}{CYAN}=================================================={RESET}\n")

    display_prompt = f"{BOLD}{prompt_label}{RESET}"
    if default_value:
        display_prompt += f" [{CYAN}{default_value}{RESET}]: "
    else:
        display_prompt += ": "

    try:
        val = input(display_prompt).strip()
        if not val and default_value:
            return default_value
        return val
    except (KeyboardInterrupt, EOFError):
        print(f"\n{YELLOW}Operación cancelada.{RESET}")
        sys.exit(0)


def load_last_ip():
    """Carga la última IP utilizada si existe el archivo."""
    if os.path.isfile(IP_HISTORY_FILE):
        try:
            with open(IP_HISTORY_FILE, "r", encoding="utf-8") as f:
                ip = f.read().strip()
                if ip:
                    return ip
        except Exception:
            pass
    return "192.168.1."


def save_last_ip(ip):
    """Guarda la IP utilizada."""
    try:
        with open(IP_HISTORY_FILE, "w", encoding="utf-8") as f:
            f.write(ip.strip())
    except Exception:
        pass


# Local CIA that `make cia` produces (fixed name, no version suffix).
CIA_PATH = os.path.join(PLATFORM_3DS, "mzm-3ds.cia")


def make_version(make_bin, env):
    """VERSION as the Makefile computes it (`make -C platform/3ds print-version`).

    NOT platform/3ds/version.txt (a stale remnant): the Makefile derives
    VERSION from git (see its GIT_VERSION block), and that is the string
    `make ftp` puts in the uploaded CIA name mzm-3ds-<VERSION>.cia. The cmake
    platform/3ds/build.sh now resolves its version the same way (via this
    same `make print-version`). Returns "" if it can't be determined.
    """
    try:
        proc = subprocess.run(
            [make_bin, "-C", PLATFORM_3DS, "-s", "--no-print-directory", "print-version"],
            env=env, capture_output=True, text=True,
        )
        for line in reversed(proc.stdout.splitlines()):
            line = line.strip()
            if line:
                return line
    except Exception:
        pass
    return ""


def report_artifact(send_ftp, ftp_host, ftp_port, remote_name):
    """Print the name and location of the generated CIA (and its FTP
    destination when it was uploaded). Called on a successful build so the
    result is visible on both the Windows (.bat) and Linux (.sh) wrappers,
    which both funnel through this script. `remote_name` is the exact
    basename `make ftp` uploaded (from parsing make's own output, or from
    make_version() as a fallback); ignored when not sending by FTP."""
    if os.path.isfile(CIA_PATH):
        size_mb = os.path.getsize(CIA_PATH) / (1024 * 1024)
        mtime = datetime.datetime.fromtimestamp(
            os.path.getmtime(CIA_PATH)
        ).strftime("%Y-%m-%d %H:%M:%S")
        print(f"  {BOLD}CIA generado:{RESET}    {os.path.basename(CIA_PATH)}  "
              f"({size_mb:.2f} MB, {mtime})")
        print(f"  {BOLD}Guardado en:{RESET}     {CIA_PATH}")
    else:
        print(f"  {YELLOW}Aviso: no se encontró el CIA esperado en {CIA_PATH}{RESET}")

    if send_ftp:
        name = remote_name or "mzm-3ds.cia"
        print(f"  {BOLD}Enviado por FTP:{RESET} "
              f"ftp://{ftp_host}:{ftp_port}/cias/{name}")


def find_make():
    """Localiza el ejecutable de make en el sistema o dentro de devkitPro."""
    make_bin = shutil.which("make")
    if make_bin:
        return make_bin

    devkitpro = os.environ.get("DEVKITPRO", "/opt/devkitpro" if os.name != "nt" else r"C:\devkitPro")
    candidates = [
        os.path.join(devkitpro, "msys2", "usr", "bin", "make.exe"),
        os.path.join(devkitpro, "devkitARM", "bin", "make.exe"),
        os.path.join(devkitpro, "tools", "bin", "make.exe"),
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c
    return "make"


def extract_errors(output_text):
    """Extrae las líneas relevantes de error de la salida de make."""
    error_lines = []
    for line in output_text.splitlines():
        line_clean = line.strip()
        if not line_clean:
            continue
        # Descartar fragmentos de código fuente que GCC imprime con número de línea (ej. "70 | ...")
        if re.match(r"^\d+\s*\|", line_clean):
            continue
        # Descartar warnings que no contengan error
        if "warning:" in line.lower() and "error:" not in line.lower():
            continue
        if any(marker in line.lower() for marker in ("error:", "error 127", "error 1", "error 2", "not found", "no such file", "undefined reference", "fatal error:")):
            if "espera a que terminen otras tareas" not in line.lower() and "sale del directorio" not in line.lower():
                if line_clean not in error_lines:
                    error_lines.append(line_clean)
    return error_lines


PORTLIB_PACKAGES = "3ds-curl 3ds-mbedtls 3ds-zlib"
PORTLIB_FILES = ("libcurl.a", "libmbedtls.a", "libmbedx509.a", "libmbedcrypto.a", "libz.a")


def missing_portlibs():
    """Names of the 3DS portlib archives (used by the self-updater) not installed."""
    default = "/opt/devkitpro" if os.name != "nt" else r"C:\devkitPro"
    libdir = os.path.join(os.environ.get("DEVKITPRO", default), "portlibs", "3ds", "lib")
    return [f for f in PORTLIB_FILES if not os.path.isfile(os.path.join(libdir, f))]


def print_portlibs_hint(missing):
    installer = "pacman" if os.name == "nt" else "dkp-pacman"
    where = " (en la shell MSYS2 de devkitPro)" if os.name == "nt" else " (puede requerir sudo)"
    print(f"\n{BOLD}{YELLOW}Faltan librerias de la 3DS:{RESET} {', '.join(missing)}")
    print(f"  El actualizador automatico las necesita. Instalalas con:{where}")
    print(f"    {BOLD}{installer} -S {PORTLIB_PACKAGES}{RESET}\n")


# Donde --install deja el CIA en la consola y la orden que lo instala (DEBUG -> HERRAMIENTAS ->
# FTP SERVER en un build debug en marcha, platform/3ds/source/ftp_server_3ds.h).
INSTALL_SD_PATH = "/3ds/Metroid Zero Mission 3DS/update/dev.cia"


def install_into_running_game(cia, host, port, dry_run=False):
    """Sube el CIA al servidor FTP del propio juego y manda 'SITE INSTALL' (el '-' lo envía tras la
    transferencia): el juego se instala sobre sí mismo y se relanza. Devuelve el código de salida."""
    from urllib.parse import quote
    cmd = ["curl", "--max-time", "300", "--ftp-create-dirs", "-T", cia,
           f"ftp://{host}:{port}{quote(INSTALL_SD_PATH)}", "-Q", f"-SITE INSTALL {INSTALL_SD_PATH}"]
    print(f"{BOLD}{CYAN}[+]{RESET} Enviando a {host}:{port} e instalando sobre el juego en marcha...")
    if dry_run:
        print(f"      {DIM}{' '.join(cmd)}{RESET}")
        return 0
    ret = subprocess.run(cmd)
    if ret.returncode != 0:
        print(f"\n{BOLD}{RED}✗ No se pudo instalar: ¿está DEBUG -> HERRAMIENTAS -> FTP SERVER encendido?{RESET}")
    else:
        print(f"\n{BOLD}{GREEN}✓ Instalado y relanzado en {host}. Espera unos segundos a que vuelva el FTP.{RESET}")
    return ret.returncode


def run_build(mode, send_ftp=False, ftp_host="", ftp_port=5000, clean=True, dry_run=False, jobs=None, verbose=False,
              install=False):
    """Ejecuta la compilación según los parámetros indicados."""
    if jobs is None:
        jobs = os.cpu_count() or 4

    debug_tools = 1 if mode in ("test", "debug") else 0
    mode_name = "Debug / Test (DEBUG_TOOLS=1)" if debug_tools == 1 else "Producción (DEBUG_TOOLS=0)"

    print(f"\n{BOLD}{CYAN}=================================================={RESET}")
    print(f"{BOLD}{WHITE} Resumen de Compilación 3DS{RESET}")
    print(f"{BOLD}{CYAN}=================================================={RESET}")
    print(f"  {BOLD}Modo:{RESET}       {GREEN if debug_tools == 1 else MAGENTA}{mode_name}{RESET}")
    print(f"  {BOLD}Limpiar:{RESET}    {'Sí (make clean)' if clean else 'No (incremental)'}")
    if send_ftp:
        print(f"  {BOLD}Envío FTP:{RESET}  {YELLOW}Sí ➔ {ftp_host}:{ftp_port}{RESET}"
              + (f" {GREEN}+ instalar sobre el juego en marcha{RESET}" if install else ""))
    else:
        print(f"  {BOLD}Envío FTP:{RESET}  No (solo .cia local)")
    print(f"  {BOLD}Hilos (jobs):{RESET} {jobs}")
    if verbose:
        print(f"  {BOLD}Salida:{RESET}     Detallada (verbose)")
    else:
        print(f"  {BOLD}Salida:{RESET}     Limpia (usa -v para salida detallada)")
    print(f"{BOLD}{CYAN}=================================================={RESET}\n")

    make_bin = find_make()
    env = os.environ.copy()
    
    # Asegurar rutas a herramientas (bannertool, makerom, etc.)
    tool_paths = [
        os.path.join(ROOT, "tools", "bin"),
        os.path.join(os.path.expanduser("~"), "tools", "bin"),
        os.path.join(os.environ.get("DEVKITPRO", "/opt/devkitpro"), "tools", "bin"),
        os.path.join(os.environ.get("DEVKITPRO", "/opt/devkitpro"), "devkitARM", "bin"),
    ]
    extra_path = os.pathsep.join([p for p in tool_paths if os.path.isdir(p)])
    if extra_path:
        env["PATH"] = extra_path + os.pathsep + env.get("PATH", "")

    missing = missing_portlibs()
    if missing:
        print_portlibs_hint(missing)
        if not dry_run:
            return 1

    if send_ftp and not install:
        env["FTP_HOST"] = ftp_host
        env["FTP_PORT"] = str(ftp_port)

    # Paso 1: Limpieza si se solicita
    if clean:
        clean_cmd = [make_bin, "-C", PLATFORM_3DS, "clean"]
        print(f"{BOLD}{CYAN}[1/2]{RESET} Limpiando objetos anteriores (make clean)...")
        if dry_run:
            print(f"      {DIM}{' '.join(clean_cmd)}{RESET}")
        else:
            if verbose:
                ret = subprocess.run(clean_cmd, env=env)
                if ret.returncode != 0:
                    print(f"\n{RED}Error durante 'make clean'.{RESET}")
                    return ret.returncode
            else:
                proc = subprocess.run(clean_cmd, env=env, capture_output=True, text=True)
                if proc.returncode != 0:
                    print(f"\n{BOLD}{RED}✗ Error durante 'make clean':{RESET}")
                    errs = extract_errors(proc.stderr + "\n" + proc.stdout)
                    for err in errs or [proc.stderr.strip()]:
                        print(f"  {RED}• {err}{RESET}")
                    return proc.returncode

    # Paso 2: Compilación (y envío si aplica)
    target = "ftp" if send_ftp and not install else "cia"
    build_cmd = [
        make_bin,
        "-C",
        PLATFORM_3DS,
        f"-j{jobs}",
        f"DEBUG_TOOLS={debug_tools}",
        target
    ]

    action_label = f"Compilando y enviando por FTP a {ftp_host}:{ftp_port}" if send_ftp and not install else "Compilando binario 3DS y empaquetando CIA"
    step_num = "[2/2]" if clean else "[1/1]"
    print(f"{BOLD}{CYAN}{step_num}{RESET} {action_label}...")

    if dry_run:
        print(f"      {DIM}{' '.join(build_cmd)}{RESET}")
        print(f"\n{GREEN}[Dry-Run] Simulación completada correctamente.{RESET}")
        return 0

    if verbose:
        ret = subprocess.run(build_cmd, env=env)
        returncode = ret.returncode
        full_output = ""
    else:
        proc = subprocess.run(build_cmd, env=env, capture_output=True, text=True)
        returncode = proc.returncode
        full_output = proc.stdout + "\n" + proc.stderr

    if returncode == 0 and install:
        cia = os.path.join(PLATFORM_3DS, "mzm-3ds.cia")
        rc = install_into_running_game(cia, ftp_host, ftp_port)
        report_artifact(False, ftp_host, ftp_port, "")
        return rc
    if returncode == 0:
        remote_name = ""
        if send_ftp:
            # Ground truth: the `ftp` recipe echoes
            #   Uploading <local>.cia as <remote>.cia to FTP...
            m = re.search(r"\bas\s+(mzm-3ds-\S+\.cia)\s+to FTP", full_output or "")
            remote_name = m.group(1) if m else ""
            if not remote_name:
                v = make_version(make_bin, env)
                remote_name = f"mzm-3ds-{v}.cia" if v else ""
            print(f"\n{BOLD}{GREEN}✓ Compilación y subida por FTP a {ftp_host}:{ftp_port} completadas con éxito!{RESET}")
        else:
            print(f"\n{BOLD}{GREEN}✓ Compilación completada con éxito!{RESET}")
        report_artifact(send_ftp, ftp_host, ftp_port, remote_name)
    else:
        print(f"\n{BOLD}{RED}✗ Error durante la compilación:{RESET}")
        if not verbose and full_output:
            errs = extract_errors(full_output)
            if errs:
                print(f"{DIM}--------------------------------------------------{RESET}")
                for err in errs:
                    print(f"  {RED}• {err}{RESET}")
                print(f"{DIM}--------------------------------------------------{RESET}")
            else:
                # Si no se filtró nada específico, mostrar últimas líneas de stderr
                last_lines = [l for l in full_output.strip().splitlines() if l.strip()][-6:]
                print(f"{DIM}--------------------------------------------------{RESET}")
                for l in last_lines:
                    print(f"  {RED}{l}{RESET}")
                print(f"{DIM}--------------------------------------------------{RESET}")
            print(f"{DIM}(Ejecuta con -v o --verbose si deseas ver la salida completa del compilador){RESET}")

    return returncode


def parse_ftp_argument(ftp_str, explicit_ip=None, default_port=5000):
    """Extrae host y puerto de strings tipo '192.168.1.50' o '192.168.1.50:5000'."""
    raw = explicit_ip or ftp_str or ""
    raw = raw.strip()
    if not raw or raw.lower() == "true":
        return "", default_port

    if ":" in raw:
        parts = raw.split(":", 1)
        host = parts[0].strip()
        try:
            port = int(parts[1].strip())
        except ValueError:
            port = default_port
        return host, port
    return raw, default_port


def local_ipv4():
    """Best-effort local IPv4. A UDP 'connect' picks the outbound interface
    without sending a packet; returns None if that can't be resolved."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return None
    finally:
        s.close()


def probe_ftp(host, port, timeout=0.4, want_banner=True):
    """Return `host` when a TCP connection opens (and, when want_banner is set,
    the peer sends an FTP '220' greeting); otherwise return None."""
    try:
        with socket.create_connection((host, port), timeout=timeout) as c:
            if not want_banner:
                return host
            c.settimeout(timeout)
            try:
                banner = c.recv(64)
            except OSError:
                return None
            return host if banner.startswith(b"220") else None
    except OSError:
        return None


def scan_ftp_hosts(port=5000, base_ip=None, timeout=0.4, workers=128):
    """Scan the local /24 for hosts answering on the FTP port. Falls back to the
    last-used IP's prefix when the local address can't be determined. Returns a
    list of IP strings sorted by last octet."""
    base_ip = base_ip or local_ipv4()
    if not base_ip:
        last = load_last_ip()
        base_ip = last if last and last.count(".") == 3 else None
    if not base_ip:
        return []
    prefix = base_ip.rsplit(".", 1)[0]
    targets = [f"{prefix}.{i}" for i in range(1, 255)]
    found = []
    with ThreadPoolExecutor(max_workers=workers) as pool:
        for res in pool.map(lambda h: probe_ftp(h, port, timeout), targets):
            if res:
                found.append(res)
    return sorted(found, key=lambda ip: int(ip.rsplit(".", 1)[1]))


def prompt_ftp_host_manual(default_ip, default_port):
    """Ask for the 3DS IP by hand until a valid one is given."""
    while True:
        ip_input = prompt_text(
            "Introduce la dirección IP de la Nintendo 3DS",
            default_value=default_ip,
        )
        host, port = parse_ftp_argument(ip_input, default_port=default_port)
        if host:
            save_last_ip(host)
            return host, port
        print(f"{RED}Error: Debes introducir una dirección IP válida.{RESET}")


# Sentinel returned by interactive_select when the retry hotkey is pressed.
# A string can never collide with the integer indices it otherwise returns.
RETRY = "RETRY"


def detect_ftp_host_interactive(default_port):
    """Scan the LAN for FTP servers and let the user pick one. The scan can be
    repeated from either outcome -- the console is often still booting ftpd
    when the first sweep runs -- and manual entry is always available."""
    last_ip = load_last_ip()

    while True:
        clear_screen()
        print(f"{BOLD}{CYAN}=================================================={RESET}")
        print(f"{BOLD}{WHITE} Metroid Zero Mission 3DS - Asistente de Build{RESET}")
        print(f"{BOLD}{CYAN}=================================================={RESET}\n")
        print(f"{BOLD}Buscando Nintendo 3DS con FTP en la red local...{RESET}")
        print(f"{DIM}(puerto {default_port}; suele tardar uno o dos segundos){RESET}")

        hosts = scan_ftp_hosts(port=default_port)

        if not hosts:
            idx = interactive_select(
                "No se encontró ninguna 3DS con FTP activo.",
                [("Introducir la IP manualmente", "")],
                subtitle="Comprueba que FBI o ftpd está abierto y en la misma Wi-Fi.",
                hotkeys={"r": RETRY},
            )
            if idx == RETRY:
                continue
            return prompt_ftp_host_manual(last_ip, default_port)

        options = [
            (h, "Última IP utilizada" if h == last_ip else "")
            for h in hosts
        ]
        options.append(("Introducir otra IP manualmente", ""))
        default_index = hosts.index(last_ip) if last_ip in hosts else 0

        idx = interactive_select(
            "Se encontraron estas 3DS con FTP. ¿A cuál quieres enviar el CIA?",
            options,
            default_index=default_index,
            hotkeys={"r": RETRY},
        )
        if idx == RETRY:
            continue
        if idx == len(hosts):
            return prompt_ftp_host_manual(last_ip, default_port)

        host = hosts[idx]
        save_last_ip(host)
        return host, default_port


def main():
    parser = argparse.ArgumentParser(
        description="Compila Metroid Zero Mission 3DS en modo Debug o Producción, con opción de envío FTP.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""Ejemplos de uso:
  # Menú interactivo:
  python3 tools/build_3ds.py

  # Compilar en modo debug/test:
  python3 tools/build_3ds.py --mode test

  # Compilar en modo producción:
  python3 tools/build_3ds.py --mode prod

  # Compilar y enviar por FTP (modo debug por defecto):
  python3 tools/build_3ds.py --ftp 192.168.1.55

  # Compilar e instalar sobre el juego en marcha (DEBUG -> HERRAMIENTAS -> FTP SERVER encendido):
  python3 tools/build_3ds.py --install 192.168.1.55 --no-clean

  # Compilar en producción y enviar por FTP a puerto específico:
  python3 tools/build_3ds.py --mode prod --ftp 192.168.1.55:5000
"""
    )
    parser.add_argument(
        "-m", "--mode",
        choices=["test", "debug", "prod", "production", "release"],
        help="Modo de compilación: 'test'/'debug' (DEBUG_TOOLS=1) o 'prod'/'production'/'release' (DEBUG_TOOLS=0)."
    )
    parser.add_argument(
        "-f", "--ftp",
        nargs="?",
        const="true",
        default=None,
        metavar="IP[:PUERTO]",
        help="Habilita envío por FTP a la 3DS. Puede especificarse la IP (ej. 192.168.1.50 o 192.168.1.50:5000)."
    )
    parser.add_argument(
        "--install",
        nargs="?",
        const="true",
        default=None,
        metavar="IP[:PUERTO]",
        help="Sube el CIA al servidor FTP del propio juego (DEBUG -> HERRAMIENTAS -> FTP SERVER) y lo "
             "instala: el juego se actualiza y se relanza. Solo builds debug."
    )
    parser.add_argument(
        "--ip",
        metavar="IP",
        help="Dirección IP de la Nintendo 3DS para el envío FTP."
    )
    parser.add_argument(
        "-p", "--port",
        type=int,
        default=5000,
        help="Puerto FTP de la Nintendo 3DS (por defecto: 5000)."
    )
    parser.add_argument(
        "-c", "--clean",
        action="store_true",
        default=None,
        help="Forzar 'make clean' antes de compilar."
    )
    parser.add_argument(
        "--no-clean",
        action="store_true",
        help="Omitir 'make clean' (compilación incremental)."
    )
    parser.add_argument(
        "-j", "--jobs",
        type=int,
        default=None,
        help="Número de procesos paralelos para make."
    )
    parser.add_argument(
        "-i", "--interactive",
        action="store_true",
        help="Forzar menú interactivo incluso con parámetros."
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Muestra toda la salida detallada del compilador."
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Muestra las órdenes a ejecutar sin realizarlas."
    )

    args = parser.parse_args()

    # Si se pide interactivo o no se pasan argumentos y es una terminal TTY:
    is_interactive = args.interactive or (
        args.mode is None and args.ftp is None and args.ip is None and args.install is None and sys.stdin.isatty()
    )

    if is_interactive:
        # Menú 1: Modo de compilación
        mode_options = [
            ("Modo Debug / Test (Recomendado para pruebas)",
             "Activa menú DEBUG inferior, herramientas de testeo y diagnósticos (DEBUG_TOOLS=1)."),
            ("Modo Producción / Release",
             "Binario limpio y optimizado para jugar (DEBUG_TOOLS=0).")
        ]
        mode_idx = interactive_select(
            "Selecciona el modo de compilación:",
            mode_options,
            default_index=0
        )
        selected_mode = "test" if mode_idx == 0 else "prod"

        # Menú 2: Enviar por FTP
        ftp_options = [
            ("Instalar sobre el juego en marcha",
             "El juego tiene DEBUG -> HERRAMIENTAS -> FTP SERVER encendido: se instala y se relanza. Sin FBI."),
            ("Sí, detectar automáticamente las 3DS con FTP en la red",
             "Escanea la subred local en busca de servidores FTP (FBI / ftpd) y te deja elegir."),
            ("Sí, introducir la IP de la 3DS manualmente",
             "Requiere tener la 3DS encendida con FBI o FTPD en la misma red Wi-Fi."),
            ("No, solo compilar el archivo .cia localmente",
             "El archivo .cia quedará en platform/3ds/mzm-3ds.cia"),
        ]
        ftp_idx = interactive_select(
            "¿Deseas enviar el CIA por FTP a tu Nintendo 3DS?",
            ftp_options,
            default_index=0
        )
        install = ftp_idx == 0
        send_ftp = ftp_idx in (0, 1, 2)
        if install:
            selected_mode = "test"  # el servidor FTP del juego solo existe en builds debug

        ftp_host = ""
        ftp_port = args.port or 5000

        if ftp_idx in (0, 1):
            ftp_host, ftp_port = detect_ftp_host_interactive(ftp_port)
        elif ftp_idx == 2:
            ftp_host, ftp_port = prompt_ftp_host_manual(load_last_ip(), ftp_port)

        # Menú 3: Clean build
        clean_options = [
            ("Sí (Limpiar antes de compilar - Recomendado)",
             "Ejecuta 'make clean' para asegurar que no haya objetos (.o) desactualizados."),
            ("No (Compilación incremental)",
             "Más rápido si solo has modificado pocos archivos.")
        ]
        clean_idx = interactive_select(
            "¿Deseas realizar una compilación limpia (make clean)?",
            clean_options,
            default_index=0
        )
        do_clean = (clean_idx == 0)

        return run_build(
            mode=selected_mode,
            send_ftp=send_ftp,
            ftp_host=ftp_host,
            ftp_port=ftp_port,
            clean=do_clean,
            dry_run=args.dry_run,
            jobs=args.jobs,
            verbose=args.verbose,
            install=install and bool(ftp_host)
        )

    # Modo No Interactivo (CLI)
    install = args.install is not None
    if install and args.mode in ("prod", "production", "release"):
        print(f"{RED}--install necesita un build debug (el servidor FTP está en DEBUG -> HERRAMIENTAS).{RESET}")
        sys.exit(1)
    send_ftp = (args.ftp is not None) or (args.ip is not None) or install
    ftp_host, ftp_port = parse_ftp_argument(args.install if install else args.ftp, explicit_ip=args.ip,
                                            default_port=args.port)

    # Regla: Por FTP, por defecto, a no ser que se diga lo contrario, siempre será en modo debug.
    if args.mode is None:
        if send_ftp:
            mode = "test"  # Default para FTP
        else:
            mode = "test"  # Default general
    else:
        mode = "test" if args.mode in ("test", "debug") else "prod"

    if send_ftp and not ftp_host:
        # Si se especificó --ftp sin IP, intentar usar la guardada o pedirla
        ftp_host = load_last_ip()
        if not ftp_host or ftp_host == "192.168.1.":
            print(f"{RED}Error: Se ha solicitado envío por FTP pero no se especificó la IP.{RESET}")
            print("Usa: --ftp 192.168.1.xxx o --ip 192.168.1.xxx")
            sys.exit(1)

    if send_ftp and ftp_host:
        save_last_ip(ftp_host)

    do_clean = True
    if args.no_clean:
        do_clean = False
    elif args.clean is not None:
        do_clean = args.clean

    return run_build(
        mode=mode,
        send_ftp=send_ftp,
        ftp_host=ftp_host,
        ftp_port=ftp_port,
        clean=do_clean,
        dry_run=args.dry_run,
        jobs=args.jobs,
        verbose=args.verbose,
        install=install
    )


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print(f"\n{YELLOW}Interrumpido por el usuario.{RESET}")
        sys.exit(130)
