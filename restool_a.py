import sys
import ctypes
import subprocess
from ctypes import wintypes

# 定数
RT_RCDATA = 10
LOAD_LIBRARY_AS_DATAFILE = 0x00000002

LANG_NEUTRAL = 0x00
SUBLANG_NEUTRAL = 0x00

def MAKELANGID(primary, sublang):
    return (sublang << 10) | primary

# ANSI版の MAKEINTRESOURCE
def MAKEINTRESOURCEA(i):
    return ctypes.cast(ctypes.c_void_p(i), wintypes.LPCSTR)

# ANSI版 API（書き込み用）
BeginUpdateResource = ctypes.windll.kernel32.BeginUpdateResourceA
BeginUpdateResource.argtypes = (wintypes.LPCSTR, wintypes.BOOL)
BeginUpdateResource.restype = wintypes.HANDLE

UpdateResource = ctypes.windll.kernel32.UpdateResourceA
UpdateResource.argtypes = (
    wintypes.HANDLE,
    wintypes.LPCSTR,
    wintypes.LPCSTR,
    wintypes.WORD,
    wintypes.LPVOID,
    ctypes.c_size_t
)
UpdateResource.restype = wintypes.BOOL

EndUpdateResource = ctypes.windll.kernel32.EndUpdateResourceA
EndUpdateResource.argtypes = (wintypes.HANDLE, wintypes.BOOL)
EndUpdateResource.restype = wintypes.BOOL

# ANSI版 API（読み込み用も統一）
LoadLibraryEx = ctypes.windll.kernel32.LoadLibraryExA
LoadLibraryEx.argtypes = (wintypes.LPCSTR, wintypes.HANDLE, wintypes.DWORD)
LoadLibraryEx.restype = wintypes.HMODULE

FindResource = ctypes.windll.kernel32.FindResourceA
FindResource.argtypes = (wintypes.HMODULE, wintypes.LPCSTR, wintypes.LPCSTR)
FindResource.restype = wintypes.HANDLE

LoadResource = ctypes.windll.kernel32.LoadResource
LoadResource.argtypes = (wintypes.HMODULE, wintypes.HANDLE)
LoadResource.restype = wintypes.HANDLE

LockResource = ctypes.windll.kernel32.LockResource
LockResource.argtypes = (wintypes.HANDLE,)
LockResource.restype = wintypes.LPVOID

SizeofResource = ctypes.windll.kernel32.SizeofResource
SizeofResource.argtypes = (wintypes.HMODULE, wintypes.HANDLE)
SizeofResource.restype = wintypes.DWORD

FreeLibrary = ctypes.windll.kernel32.FreeLibrary
FreeLibrary.argtypes = (wintypes.HMODULE,)
FreeLibrary.restype = wintypes.BOOL

GetLastError = ctypes.windll.kernel32.GetLastError
GetLastError.restype = wintypes.DWORD

#
# リソース書き込み
#
def write_rcdata_to_exe(target_exe_path: str, argv: list[str], resource_id: int = 101) -> None:
    cmdline = subprocess.list2cmdline(argv)
    data_to_write = cmdline.encode("mbcs") + b'\0'
    data_size = len(data_to_write)

    exe_path_bytes = target_exe_path.encode("mbcs")
    hUpdate = BeginUpdateResource(exe_path_bytes, False)
    if not hUpdate:
        print(f"BeginUpdateResourceA failed. Error = {GetLastError()}")
        return

    success = UpdateResource(
        hUpdate,
        MAKEINTRESOURCEA(RT_RCDATA),
        MAKEINTRESOURCEA(resource_id),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
        data_to_write,
        data_size
    )

    if not success:
        print(f"UpdateResourceA failed. Error = {GetLastError()}")
        EndUpdateResource(hUpdate, True)
        return

    if not EndUpdateResource(hUpdate, False):
        print(f"EndUpdateResourceA failed. Error = {GetLastError()}")
        return

    print("リソースの更新が完了しました。")

#
# リソース読み込み（ANSI API版）
#
def read_rcdata_text_from_exe(exe_path: str, resource_id: int = 101) -> str:
    exe_path_bytes = exe_path.encode("mbcs")
    hModule = LoadLibraryEx(exe_path_bytes, None, LOAD_LIBRARY_AS_DATAFILE)
    if not hModule:
        err = GetLastError()
        raise OSError(f"LoadLibraryExA failed. Error={err}")

    try:
        hResInfo = FindResource(hModule, MAKEINTRESOURCEA(resource_id), MAKEINTRESOURCEA(RT_RCDATA))
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
        raw_data = data_pointer.contents
        byte_data = bytes(raw_data)

        text = byte_data.decode(errors='replace')
        text = text.rstrip('\x00')
        return text

    finally:
        FreeLibrary(hModule)

#
# メイン
#
def main():
    if len(sys.argv) < 3:
        print("Usage:")
        print("  python unified.py read  <exe_path>")
        print("  python unified.py write <exe_path> <argv_prefix...>")
        sys.exit(1)

    mode = sys.argv[1].lower()
    exe_path = sys.argv[2]

    if mode == "read":
        current_text = read_rcdata_text_from_exe(exe_path, resource_id=101)
        if current_text is None:
            print("RT_RCDATA(ID=101) が存在しないか、読み込みに失敗しました。")
        else:
            print(current_text)

    elif mode == "write":
        argv_prefix = sys.argv[3:]
        write_rcdata_to_exe(exe_path, argv_prefix, resource_id=101)

    else:
        print(f"不明なコマンド: {mode}")
        print("read または write を指定してください。")
        sys.exit(1)

if __name__ == "__main__":
    main()
