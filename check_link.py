import ctypes
from ctypes import wintypes

RT_RCDATA = 10
LOAD_LIBRARY_AS_DATAFILE = 0x00000002

def MAKEINTRESOURCE(i):
    return ctypes.cast(ctypes.c_void_p(i), wintypes.LPCWSTR)

LoadLibraryExW = ctypes.windll.kernel32.LoadLibraryExW
LoadLibraryExW.argtypes = (wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD)
LoadLibraryExW.restype = wintypes.HMODULE

FindResourceW = ctypes.windll.kernel32.FindResourceW
FindResourceW.argtypes = (wintypes.HMODULE, wintypes.LPCWSTR, wintypes.LPCWSTR)
FindResourceW.restype = wintypes.HANDLE  # HRSRC

LoadResource = ctypes.windll.kernel32.LoadResource
LoadResource.argtypes = (wintypes.HMODULE, wintypes.HANDLE)
LoadResource.restype = wintypes.HANDLE  # HGLOBAL

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

def read_rcdata_text_from_exe(exe_path: str, resource_id: int = 101) -> str:
    """
    指定 exe から RT_RCDATA の指定リソース ID を読み込み、テキスト(Unicode)として返す。
    リソースが存在しない場合は None を返す。
    """
    hModule = LoadLibraryExW(exe_path, None, LOAD_LIBRARY_AS_DATAFILE)
    if not hModule:
        err = GetLastError()
        raise OSError(f"LoadLibraryExW failed. Error={err}")

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

        # pResource をバイナリとして取り出す
        # 1) ctypes.cast(...) で "char配列へのポインタ" に変換
        # 2) その .contents から Python の bytes に変換
        data_pointer = ctypes.cast(pResource, ctypes.POINTER(ctypes.c_char * size))
        raw_data = data_pointer.contents  # c_char * size
        byte_data = bytes(raw_data)       # → Python の bytes オブジェクト

        # リソースが UTF-16(LE) で格納されている想定の場合
        # (BOM 無しの可能性も考慮し、'utf-16-le' で直接デコード)
        # text = byte_data.decode('utf-16-le', errors='replace')
        text = byte_data.decode(errors='replace')

        # リソースに null 終端が入っている場合は末尾の '\x00' を除去
        text = text.rstrip('\x00')
        return text

    finally:
        FreeLibrary(hModule)

if __name__ == "__main__":
    import sys
    if len(sys.argv) < 2:
        print("Usage: python check.py <path_to_exe>")
        sys.exit(1)

    targetExePath = sys.argv[1]
    current_text = read_rcdata_text_from_exe(targetExePath, resource_id=101)
    if current_text is None:
        print("RT_RCDATA の ID=101 リソースが存在しないか、読み込みに失敗しました。")
    else:
        print("【既存リソースの内容】")
        print(current_text)
