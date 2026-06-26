import sys
import ctypes
import ctypes.wintypes
import os
import argparse
import struct
import subprocess
import tempfile


RT_RCDATA = 10
LOAD_LIBRARY_AS_DATAFILE = 0x00000002

LANG_NEUTRAL = 0x00
SUBLANG_NEUTRAL = 0x00

def MAKELANGID(primary, sublang):
    return (sublang << 10) | primary

def MAKEINTRESOURCE(i):
    return ctypes.cast(ctypes.c_void_p(i), ctypes.wintypes.LPCWSTR)

# --- WinAPI ---
BeginUpdateResource = ctypes.windll.kernel32.BeginUpdateResourceW
BeginUpdateResource.argtypes = (ctypes.wintypes.LPCWSTR, ctypes.wintypes.BOOL)
BeginUpdateResource.restype = ctypes.wintypes.HANDLE

UpdateResource = ctypes.windll.kernel32.UpdateResourceW
UpdateResource.argtypes = (
    ctypes.wintypes.HANDLE,
    ctypes.wintypes.LPCWSTR,
    ctypes.wintypes.LPCWSTR,
    ctypes.wintypes.WORD,
    ctypes.wintypes.LPVOID,
    ctypes.c_size_t
)
UpdateResource.restype = ctypes.wintypes.BOOL

EndUpdateResource = ctypes.windll.kernel32.EndUpdateResourceW
EndUpdateResource.argtypes = (ctypes.wintypes.HANDLE, ctypes.wintypes.BOOL)
EndUpdateResource.restype = ctypes.wintypes.BOOL

LoadLibraryExW = ctypes.windll.kernel32.LoadLibraryExW
LoadLibraryExW.argtypes = (ctypes.wintypes.LPCWSTR, ctypes.wintypes.HANDLE, ctypes.wintypes.DWORD)
LoadLibraryExW.restype = ctypes.wintypes.HMODULE

FindResourceW = ctypes.windll.kernel32.FindResourceW
FindResourceW.argtypes = (ctypes.wintypes.HMODULE, ctypes.wintypes.LPCWSTR, ctypes.wintypes.LPCWSTR)
FindResourceW.restype = ctypes.wintypes.HANDLE

LoadResource = ctypes.windll.kernel32.LoadResource
LoadResource.argtypes = (ctypes.wintypes.HMODULE, ctypes.wintypes.HANDLE)
LoadResource.restype = ctypes.wintypes.HANDLE

LockResource = ctypes.windll.kernel32.LockResource
LockResource.argtypes = (ctypes.wintypes.HANDLE,)
LockResource.restype = ctypes.wintypes.LPVOID

SizeofResource = ctypes.windll.kernel32.SizeofResource
SizeofResource.argtypes = (ctypes.wintypes.HMODULE, ctypes.wintypes.HANDLE)
SizeofResource.restype = ctypes.wintypes.DWORD

FreeLibrary = ctypes.windll.kernel32.FreeLibrary
FreeLibrary.argtypes = (ctypes.wintypes.HMODULE,)
FreeLibrary.restype = ctypes.wintypes.BOOL

GetLastError = ctypes.windll.kernel32.GetLastError
GetLastError.restype = ctypes.wintypes.DWORD

# --- CommandLineToArgvW for converting ARGV cmdline -> argv list ---
CommandLineToArgvW = ctypes.windll.shell32.CommandLineToArgvW
CommandLineToArgvW.argtypes = (ctypes.wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_int))
CommandLineToArgvW.restype = ctypes.POINTER(ctypes.wintypes.LPWSTR)

LocalFree = ctypes.windll.kernel32.LocalFree
LocalFree.argtypes = (ctypes.wintypes.HLOCAL,)
LocalFree.restype = ctypes.wintypes.HLOCAL

# --- Windows profile/INI API ---
GetPrivateProfileStringW = ctypes.windll.kernel32.GetPrivateProfileStringW
GetPrivateProfileStringW.argtypes = (
    ctypes.wintypes.LPCWSTR,
    ctypes.wintypes.LPCWSTR,
    ctypes.wintypes.LPCWSTR,
    ctypes.wintypes.LPWSTR,
    ctypes.wintypes.DWORD,
    ctypes.wintypes.LPCWSTR,
)
GetPrivateProfileStringW.restype = ctypes.wintypes.DWORD

GetPrivateProfileSectionW = ctypes.windll.kernel32.GetPrivateProfileSectionW
GetPrivateProfileSectionW.argtypes = (
    ctypes.wintypes.LPCWSTR,
    ctypes.wintypes.LPWSTR,
    ctypes.wintypes.DWORD,
    ctypes.wintypes.LPCWSTR,
)
GetPrivateProfileSectionW.restype = ctypes.wintypes.DWORD

WritePrivateProfileStringW = ctypes.windll.kernel32.WritePrivateProfileStringW
WritePrivateProfileStringW.argtypes = (
    ctypes.wintypes.LPCWSTR,
    ctypes.wintypes.LPCWSTR,
    ctypes.wintypes.LPCWSTR,
    ctypes.wintypes.LPCWSTR,
)
WritePrivateProfileStringW.restype = ctypes.wintypes.BOOL


CONFIG_RESOURCE_ID = 101

# type: UTF-16LE 4-WCHAR (8 bytes)
TYPE_ARGV = b"A\x00R\x00G\x00V\x00"          # "ARGV"
TYPE_ENV  = b"E\x00N\x00V\x00\x00\x00"      # "ENV\0"
TYPE_END  = b"E\x00N\x00D\x00\x00\x00"      # "END\0"

def _u64(n: int) -> bytes:
    return struct.pack("<Q", n)

def _read_u64(blob: bytes, off: int) -> tuple[int, int]:
    if off + 8 > len(blob):
        raise ValueError("config blob truncated (u64)")
    (v,) = struct.unpack_from("<Q", blob, off)
    return v, off + 8

def write_rcdata_bytes_to_exe(target_exe_path: str, blob: bytes, resource_id: int) -> None:
    hUpdate = BeginUpdateResource(target_exe_path, False)
    if not hUpdate:
        raise OSError(f"BeginUpdateResource failed. Error={GetLastError()}")

    success = UpdateResource(
        hUpdate,
        MAKEINTRESOURCE(RT_RCDATA),
        MAKEINTRESOURCE(resource_id),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
        blob,
        len(blob)
    )
    if not success:
        EndUpdateResource(hUpdate, True)
        raise OSError(f"UpdateResource failed. Error={GetLastError()}")

    if not EndUpdateResource(hUpdate, False):
        raise OSError(f"EndUpdateResource failed. Error={GetLastError()}")

def read_rcdata_bytes_from_exe(exe_path: str, resource_id: int) -> bytes | None:
    hModule = LoadLibraryExW(exe_path, None, LOAD_LIBRARY_AS_DATAFILE)
    if not hModule:
        raise OSError(f"LoadLibraryExW failed. Error={GetLastError()}")

    try:
        hResInfo = FindResourceW(hModule, MAKEINTRESOURCE(resource_id), MAKEINTRESOURCE(RT_RCDATA))
        if not hResInfo:
            return None

        size = SizeofResource(hModule, hResInfo)
        if size == 0:
            return None

        hResData = LoadResource(hModule, hResInfo)
        if not hResData:
            return None

        pResource = LockResource(hResData)
        if not pResource:
            return None

        data_pointer = ctypes.cast(pResource, ctypes.POINTER(ctypes.c_char * size))
        return bytes(data_pointer.contents)
    finally:
        FreeLibrary(hModule)

def cmdline_to_argv(cmdline: str) -> list[str]:
    argc = ctypes.c_int(0)
    argv_ptr = CommandLineToArgvW(cmdline, ctypes.byref(argc))
    if not argv_ptr:
        raise OSError(f"CommandLineToArgvW failed. Error={GetLastError()}")
    try:
        return [argv_ptr[i] for i in range(argc.value)]
    finally:
        LocalFree(argv_ptr)

def build_config_blob(argv_list: list[str], env_map: dict[str, str]) -> bytes:
    # argv list -> windows command-line string
    argv_cmdline = subprocess.list2cmdline(argv_list)
    argv_bytes = argv_cmdline.encode("utf-16-le")

    out = bytearray()
    out += TYPE_ARGV
    out += _u64(len(argv_bytes))
    out += argv_bytes

    # env: stable output order (sorted by key) for reproducibility
    for k in sorted(env_map.keys()):
        v = env_map[k]
        if not isinstance(k, str) or k == "":
            raise ValueError("env key must be non-empty string")
        if not isinstance(v, str):
            raise ValueError(f"env[{k}] value must be string")

        key_b = k.encode("utf-16-le")
        val_b = v.encode("utf-16-le")  # may be empty -> len==0 => set empty string

        out += TYPE_ENV
        out += _u64(len(key_b))
        out += key_b
        out += _u64(len(val_b))
        out += val_b

    out += TYPE_END
    return bytes(out)

def parse_config_blob(blob: bytes) -> tuple[list[str], dict[str, str]]:
    i = 0
    n = len(blob)

    def need(k: int) -> None:
        if i + k > n:
            raise ValueError("config blob truncated")

    argv_cmdline: str | None = None
    env_map: dict[str, str] = {}

    while True:
        need(8)
        t = blob[i:i+8]
        i += 8

        if t == TYPE_END:
            break

        if t == TYPE_ARGV:
            l, i = _read_u64(blob, i)
            need(l)
            b = blob[i:i+l]
            i += l
            argv_cmdline = b.decode("utf-16-le", errors="strict")
            continue

        if t == TYPE_ENV:
            klen, i = _read_u64(blob, i)
            need(klen)
            kb = blob[i:i+klen]
            i += klen
            key = kb.decode("utf-16-le", errors="strict")
            if key == "":
                raise ValueError("empty env key is not allowed")

            vlen, i = _read_u64(blob, i)
            need(vlen)
            vb = blob[i:i+vlen]
            i += vlen
            val = vb.decode("utf-16-le", errors="strict")  # may be ""
            env_map[key] = val
            continue

        raise ValueError(f"unknown record type: {t!r}")

    if argv_cmdline is None:
        raise ValueError("ARGV record not found")

    argv_list = cmdline_to_argv(argv_cmdline)
    return argv_list, env_map

def load_config(exe_path: str) -> tuple[list[str], dict[str, str]]:
    blob = read_rcdata_bytes_from_exe(exe_path, CONFIG_RESOURCE_ID)
    if not blob:
        raise OSError("Config (RT_RCDATA 101) not found.")
    return parse_config_blob(blob)

def write_config(exe_path: str, argv_list: list[str], env_map: dict[str, str]) -> None:
    blob = build_config_blob(argv_list, env_map)
    write_rcdata_bytes_to_exe(exe_path, blob, CONFIG_RESOURCE_ID)

def _check_profile_write(ok: bool, action: str) -> None:
    if not ok:
        raise OSError(f"{action} failed. Error={GetLastError()}")

def _profile_string(section: str, key: str, ini_path: str, default: str = "") -> str:
    # GetPrivateProfileStringW truncates when the buffer is too small. Grow until stable.
    size = 256
    while True:
        buf = ctypes.create_unicode_buffer(size)
        n = GetPrivateProfileStringW(section, key, default, buf, size, ini_path)
        if n < size - 1:
            return buf.value
        size *= 2
        if size > 1024 * 1024:
            raise ValueError(f"INI value too large: [{section}] {key}")

def _profile_section(section: str, ini_path: str) -> dict[str, str]:
    # Returns a section as KEY=VALUE entries. Buffer ends with double NUL.
    size = 4096
    while True:
        buf = ctypes.create_unicode_buffer(size)
        n = GetPrivateProfileSectionW(section, buf, size, ini_path)
        if n < size - 2:
            raw = buf[:n]
            entries = "".join(raw).split("\x00") if n else []
            out: dict[str, str] = {}
            for entry in entries:
                if not entry:
                    continue
                if "=" in entry:
                    k, v = entry.split("=", 1)
                else:
                    k, v = entry, ""
                if k:
                    out[k] = v
            return out
        size *= 2
        if size > 1024 * 1024:
            raise ValueError(f"INI section too large: [{section}]")

def _clear_profile_section(section: str, ini_path: str) -> None:
    _check_profile_write(WritePrivateProfileStringW(section, None, None, ini_path), f"clear [{section}]")

def _write_profile_value(section: str, key: str, value: str, ini_path: str) -> None:
    _check_profile_write(WritePrivateProfileStringW(section, key, value, ini_path), f"write [{section}] {key}")

def _ensure_unicode_ini_file(ini_path: str) -> None:
    # The profile API treats a new INI file as ANSI unless it already has a
    # Unicode BOM. Create new/empty files as UTF-16LE so non-ASCII argv/env
    # values round-trip through the W APIs. Existing files are left unchanged.
    if not os.path.exists(ini_path) or os.path.getsize(ini_path) == 0:
        with open(ini_path, "wb") as f:
            f.write(b"\xff\xfe")

def read_ini_config(ini_path: str) -> tuple[list[str], dict[str, str]]:
    """Read config from an INI file using the Windows profile API.

    INI schema:

        [argv]
        0=program.exe
        1=--flag
        2=value

        [env]
        NAME=value

    For compatibility, [argv] command_line=... is also accepted when numeric argv
    entries are absent.
    """
    ini_path = os.path.abspath(ini_path)
    argv_section = _profile_section("argv", ini_path)

    numeric_keys: list[tuple[int, str]] = []
    for k, v in argv_section.items():
        if k.isdecimal():
            numeric_keys.append((int(k), v))

    if numeric_keys:
        numeric_keys.sort(key=lambda x: x[0])
        expected = list(range(len(numeric_keys)))
        actual = [i for i, _ in numeric_keys]
        if actual != expected:
            raise ValueError(f"[argv] numeric keys must be contiguous from 0; got {actual}")
        argv_list = [v for _, v in numeric_keys]
    else:
        cmdline = _profile_string("argv", "command_line", ini_path, "")
        if cmdline == "":
            raise ValueError("INI must contain [argv] numeric entries or [argv] command_line")
        argv_list = cmdline_to_argv(cmdline)

    env_section = _profile_section("env", ini_path)
    env_map: dict[str, str] = {}
    for k, v in env_section.items():
        if k == "":
            raise ValueError("env keys must be non-empty strings")
        env_map[k] = v

    return argv_list, env_map

def write_ini_config(ini_path: str, argv_list: list[str], env_map: dict[str, str]) -> None:
    """Write config to an INI file using the Windows profile API."""
    ini_path = os.path.abspath(ini_path)

    if not isinstance(argv_list, list) or not all(isinstance(x, str) for x in argv_list):
        raise ValueError("argv must be list[str]")
    if not isinstance(env_map, dict):
        raise ValueError("env must be dict[str,str]")

    parent = os.path.dirname(ini_path)
    if parent:
        os.makedirs(parent, exist_ok=True)
    _ensure_unicode_ini_file(ini_path)

    _clear_profile_section("argv", ini_path)
    _clear_profile_section("env", ini_path)

    for i, arg in enumerate(argv_list):
        _write_profile_value("argv", str(i), arg, ini_path)

    for k in sorted(env_map.keys()):
        v = env_map[k]
        if not isinstance(k, str) or k == "":
            raise ValueError("env keys must be non-empty strings")
        if not isinstance(v, str):
            raise ValueError(f"env[{k}] must be a string")
        _write_profile_value("env", k, v, ini_path)

    # Flush cached profile data for this file.
    _check_profile_write(WritePrivateProfileStringW(None, None, None, ini_path), "flush INI")

def dump_as_ini(argv_list: list[str], env_map: dict[str, str]) -> str:
    # The Windows profile API writes to files, not streams. Use a temporary file
    # so stdout output still goes through WritePrivateProfileStringW.
    fd, tmp_path = tempfile.mkstemp(suffix=".ini")
    os.close(fd)
    try:
        write_ini_config(tmp_path, argv_list, env_map)
        with open(tmp_path, "r", encoding="utf-16") as f:
            data = f.read()
    except UnicodeError:
        with open(tmp_path, "r", encoding="mbcs") as f:
            data = f.read()
    finally:
        try:
            os.remove(tmp_path)
        except OSError:
            pass
    return data if data.endswith("\n") else data + "\n"

def read_ini_from_stdin() -> tuple[list[str], dict[str, str]]:
    data = sys.stdin.buffer.read()
    fd, tmp_path = tempfile.mkstemp(suffix=".ini")
    os.close(fd)
    try:
        with open(tmp_path, "wb") as f:
            f.write(data)
        return read_ini_config(tmp_path)
    finally:
        try:
            os.remove(tmp_path)
        except OSError:
            pass

def read_config_from_ini_path(path: str) -> tuple[list[str], dict[str, str]]:
    if path == "-" or path == "":
        return read_ini_from_stdin()
    return read_ini_config(path)

def main() -> int:
    parser = argparse.ArgumentParser(prog=os.path.basename(sys.argv[0]))
    subparsers = parser.add_subparsers(dest="subcmd", required=True)

    p_get = subparsers.add_parser("get", help="output config (argv/env) as INI")
    p_get.add_argument("exe_path", help="target exe path")
    p_get.add_argument("--out", dest="out_path", default="-", help="output INI file path, or '-' for stdout (default: '-')")

    p_set = subparsers.add_parser("set", help="set config from INI")
    p_set.add_argument("exe_path", help="target exe path")
    p_set.add_argument("--in", dest="in_path", default="-", help="input INI file path, or '-' for stdin (default: '-')")

    args = parser.parse_args()
    exe_path = os.path.abspath(args.exe_path)

    try:
        if args.subcmd == "get":
            argv, env = load_config(exe_path)
            if args.out_path == "-" or args.out_path == "":
                sys.stdout.write(dump_as_ini(argv, env))
            else:
                write_ini_config(args.out_path, argv, env)
            return 0

        if args.subcmd == "set":
            argv, env = read_config_from_ini_path(args.in_path)
            write_config(exe_path, argv, env)
            return 0

        parser.error("unknown subcommand")
        return 2

    except Exception as e:
        print(str(e), file=sys.stderr)
        return 1

if __name__ == "__main__":
    raise SystemExit(main())
