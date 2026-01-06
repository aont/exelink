import sys
import ctypes
import subprocess
import os
import ctypes.wintypes
import argparse

#
# Shared constants / functions / WinAPI definitions
# (keep existing definitions as-is)
#
RT_RCDATA = 10
LOAD_LIBRARY_AS_DATAFILE = 0x00000002

LANG_NEUTRAL = 0x00
SUBLANG_NEUTRAL = 0x00

def MAKELANGID(primary, sublang):
    return (sublang << 10) | primary

def MAKEINTRESOURCE(i):
    return ctypes.cast(ctypes.c_void_p(i), ctypes.wintypes.LPCWSTR)

# --- WinAPI: CommandLineToArgvW (shell32) / LocalFree (kernel32) ---
CommandLineToArgvW = ctypes.windll.shell32.CommandLineToArgvW
CommandLineToArgvW.argtypes = (ctypes.wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_int))
CommandLineToArgvW.restype = ctypes.POINTER(ctypes.wintypes.LPWSTR)

LocalFree = ctypes.windll.kernel32.LocalFree
LocalFree.argtypes = (ctypes.wintypes.HLOCAL,)
LocalFree.restype = ctypes.wintypes.HLOCAL


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


def write_rcdata_text_to_exe(target_exe_path: str, text: str, resource_id: int) -> None:
    data_to_write = text.encode("utf-16-le")
    data_size = len(data_to_write)

    hUpdate = BeginUpdateResource(target_exe_path, False)
    if not hUpdate:
        raise OSError(f"BeginUpdateResource failed. Error={GetLastError()}")

    success = UpdateResource(
        hUpdate,
        MAKEINTRESOURCE(RT_RCDATA),
        MAKEINTRESOURCE(resource_id),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
        data_to_write,
        data_size
    )

    if not success:
        EndUpdateResource(hUpdate, True)
        raise OSError(f"UpdateResource failed. Error={GetLastError()}")

    if not EndUpdateResource(hUpdate, False):
        raise OSError(f"EndUpdateResource failed. Error={GetLastError()}")


def delete_rcdata_from_exe(target_exe_path: str, resource_id: int) -> None:
    """
    Delete an RT_RCDATA resource by id. This uses UpdateResource with lpData=NULL and cbData=0.
    """
    hUpdate = BeginUpdateResource(target_exe_path, False)
    if not hUpdate:
        raise OSError(f"BeginUpdateResource failed. Error={GetLastError()}")

    success = UpdateResource(
        hUpdate,
        MAKEINTRESOURCE(RT_RCDATA),
        MAKEINTRESOURCE(resource_id),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
        None,
        0
    )

    if not success:
        EndUpdateResource(hUpdate, True)
        raise OSError(f"UpdateResource(delete) failed. Error={GetLastError()}")

    if not EndUpdateResource(hUpdate, False):
        raise OSError(f"EndUpdateResource failed. Error={GetLastError()}")


def read_rcdata_text_from_exe(exe_path: str, resource_id: int) -> str | None:
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
        byte_data = bytes(data_pointer.contents)

        text = byte_data.decode("utf-16-le", errors="strict")
        return text.rstrip("\x00")

    finally:
        FreeLibrary(hModule)

def write_cmd_prefix_to_exe(target_exe_path: str, argv_prefix: list[str], resource_id: int = 101) -> None:
    cmdline = subprocess.list2cmdline(argv_prefix)
    write_rcdata_text_to_exe(target_exe_path, cmdline, resource_id)


def write_env_to_exe(target_exe_path: str, env_items: list[str], resource_id: int = 102) -> None:
    # Store one definition per line (normalized to LF)
    text = "\n".join(env_items)
    write_rcdata_text_to_exe(target_exe_path, text, resource_id)


def _print_or_empty(msg: str | None) -> None:
    if msg is None:
        print("")
    else:
        print(msg)

def cmdline_to_argv(cmdline: str) -> list[str]:
    """
    Split a command-line string into argv using Windows CommandLineToArgvW.
    Returns argv as a Python list[str].
    """
    argc = ctypes.c_int(0)
    argv_ptr = CommandLineToArgvW(cmdline, ctypes.byref(argc))
    if not argv_ptr:
        # GetLastError is not always reliable for CommandLineToArgvW,
        # but is printed anyway.
        raise OSError(f"CommandLineToArgvW failed. Error={GetLastError()}")

    try:
        return [argv_ptr[i] for i in range(argc.value)]
    finally:
        # The buffer allocated by CommandLineToArgvW must be freed with LocalFree
        LocalFree(argv_ptr)

def main() -> int:
    parser = argparse.ArgumentParser(prog=os.path.basename(sys.argv[0]))
    subparsers = parser.add_subparsers(dest="subcmd", required=True)

    # cmd subcommand
    p_cmd = subparsers.add_parser("cmd", help="read/write command prefix resource (RT_RCDATA 101)")
    p_cmd.add_argument("exe_path", help="target exe path")
    # Remainder is used to allow arguments starting with '-'
    p_cmd.add_argument("argv_prefix", nargs=argparse.REMAINDER, help="argv prefix to write")

    # env subcommand
    p_env = subparsers.add_parser("env", help="read/write env resource (RT_RCDATA 102)")
    p_env.add_argument("exe_path", help="target exe path")
    # List NAME=VALUE as-is (no --env option).
    # Remainder is used to allow items starting with '-'.
    p_env.add_argument("env_items", nargs=argparse.REMAINDER, help="env items like NAME=VALUE (or NAME to unset)")

    args = parser.parse_args()
    exe_path = os.path.abspath(args.exe_path)

    try:
        if args.subcmd == "cmd":
            if len(args.argv_prefix) == 0:
                # Display
                text = read_rcdata_text_from_exe(exe_path, resource_id=101)
                if not text:
                    print("")
                    return 0

                argv = cmdline_to_argv(text)
                for a in argv:
                    print(a)
                return 0
            else:
                # Set
                write_cmd_prefix_to_exe(exe_path, args.argv_prefix, resource_id=101)
                return 0

        if args.subcmd == "env":
            if len(args.env_items) == 0:
                # Display
                text = read_rcdata_text_from_exe(exe_path, resource_id=102)
                _print_or_empty(text)
                return 0
            else:
                # Set / Clear
                # If args.env_items[0] == "--clear", delete RT_RCDATA 102.
                if args.env_items[0] == "--clear":
                    delete_rcdata_from_exe(exe_path, resource_id=102)
                    return 0

                # Light normalization to avoid empty leading elements (optional)
                items = [x for x in args.env_items if x != ""]
                write_env_to_exe(exe_path, items, resource_id=102)
                return 0

        parser.error("unknown subcommand")
        return 2

    except OSError as e:
        print(str(e), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
