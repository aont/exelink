import sys
import ctypes
import ctypes.wintypes
import os
import argparse
import struct
import json
import subprocess

# ---- TOML support ----
try:
    import tomllib  # Python 3.11+
except Exception:
    tomllib = None

try:
    import tomli  # optional fallback for <=3.10
except Exception:
    tomli = None


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

def dump_as_json(obj: object) -> str:
    return json.dumps(obj, ensure_ascii=False, indent=2) + "\n"

def dump_as_toml(obj: dict) -> str:
    # Minimal TOML emitter for our schema only: { argv = [...], [env] ... }
    argv = obj.get("argv", [])
    env = obj.get("env", {})

    if not isinstance(argv, list) or not all(isinstance(x, str) for x in argv):
        raise ValueError("argv must be list[str]")
    if not isinstance(env, dict) or not all(isinstance(k, str) and isinstance(v, str) for k, v in env.items()):
        raise ValueError("env must be dict[str,str]")

    def toml_quote(s: str) -> str:
        # basic escaping for TOML basic strings
        s = s.replace("\\", "\\\\").replace('"', '\\"')
        s = s.replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")
        return f'"{s}"'

    lines = []
    lines.append("argv = [" + ", ".join(toml_quote(x) for x in argv) + "]")
    lines.append("")
    lines.append("[env]")
    for k in sorted(env.keys()):
        lines.append(f"{k} = {toml_quote(env[k])}")
    lines.append("")
    return "\n".join(lines)

def parse_input_bytes(data: bytes, fmt: str) -> dict:
    if fmt == "json":
        obj = json.loads(data.decode("utf-8"))
        if not isinstance(obj, dict):
            raise ValueError("JSON root must be an object")
        return obj

    if fmt == "toml":
        lib = tomllib or tomli
        if lib is None:
            raise RuntimeError("TOML parsing requires Python 3.11+ (tomllib) or 'tomli' installed.")
        obj = lib.loads(data.decode("utf-8"))
        if not isinstance(obj, dict):
            raise ValueError("TOML root must be a table")
        return obj

    raise ValueError(f"unknown format: {fmt}")

def normalize_config_obj(obj: dict) -> tuple[list[str], dict[str, str]]:
    if "argv" not in obj:
        raise ValueError("missing required field: argv")
    argv = obj["argv"]
    env = obj.get("env", {})

    if not isinstance(argv, list) or not all(isinstance(x, str) for x in argv):
        raise ValueError("argv must be list[str]")

    if not isinstance(env, dict):
        raise ValueError("env must be an object/table (dict)")

    env_map: dict[str, str] = {}
    for k, v in env.items():
        if not isinstance(k, str) or k == "":
            raise ValueError("env keys must be non-empty strings")
        if not isinstance(v, str):
            raise ValueError(f"env[{k}] must be a string")
        env_map[k] = v

    return argv, env_map

def read_all_from_path(path: str) -> bytes:
    if path == "-" or path == "":
        return sys.stdin.buffer.read()
    with open(path, "rb") as f:
        return f.read()

def main() -> int:
    parser = argparse.ArgumentParser(prog=os.path.basename(sys.argv[0]))
    subparsers = parser.add_subparsers(dest="subcmd", required=True)

    p_get = subparsers.add_parser("get", help="output config (argv/env) as JSON or TOML")
    p_get.add_argument("exe_path", help="target exe path")
    p_get.add_argument("--format", choices=["json", "toml"], default="json", help="output format")

    p_set = subparsers.add_parser("set", help="set config from JSON or TOML")
    p_set.add_argument("exe_path", help="target exe path")
    p_set.add_argument("--format", choices=["json", "toml"], default="json", help="input format")
    p_set.add_argument("--in", dest="in_path", default="-", help="input file path, or '-' for stdin (default: '-')")

    args = parser.parse_args()
    exe_path = os.path.abspath(args.exe_path)

    try:
        if args.subcmd == "get":
            argv, env = load_config(exe_path)
            obj = {"argv": argv, "env": env}
            if args.format == "json":
                sys.stdout.write(dump_as_json(obj))
            else:
                sys.stdout.write(dump_as_toml(obj))
            return 0

        if args.subcmd == "set":
            data = read_all_from_path(args.in_path)
            obj = parse_input_bytes(data, args.format)
            argv, env = normalize_config_obj(obj)
            write_config(exe_path, argv, env)
            return 0

        parser.error("unknown subcommand")
        return 2

    except Exception as e:
        print(str(e), file=sys.stderr)
        return 1

if __name__ == "__main__":
    raise SystemExit(main())
