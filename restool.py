import sys
import ctypes
import subprocess
from ctypes import wintypes

#
# 共有で使用する定数／関数／WinAPI定義
#
RT_RCDATA = 10
LOAD_LIBRARY_AS_DATAFILE = 0x00000002

LANG_NEUTRAL = 0x00
SUBLANG_NEUTRAL = 0x00

def MAKELANGID(primary, sublang):
    return (sublang << 10) | primary

def MAKEINTRESOURCE(i):
    return ctypes.cast(ctypes.c_void_p(i), wintypes.LPCWSTR)

# -- 書き込み(更新)に使用する WinAPI ---
BeginUpdateResource = ctypes.windll.kernel32.BeginUpdateResourceW
BeginUpdateResource.argtypes = (wintypes.LPCWSTR, wintypes.BOOL)
BeginUpdateResource.restype = wintypes.HANDLE

UpdateResource = ctypes.windll.kernel32.UpdateResourceW
UpdateResource.argtypes = (
    wintypes.HANDLE,
    wintypes.LPCWSTR,
    wintypes.LPCWSTR,
    wintypes.WORD,
    wintypes.LPVOID,
    ctypes.c_size_t
)
UpdateResource.restype = wintypes.BOOL

EndUpdateResource = ctypes.windll.kernel32.EndUpdateResourceW
EndUpdateResource.argtypes = (wintypes.HANDLE, wintypes.BOOL)
EndUpdateResource.restype = wintypes.BOOL

# -- 読み込みに使用する WinAPI ---
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

# エラーコード取得
GetLastError = ctypes.windll.kernel32.GetLastError
GetLastError.restype = wintypes.DWORD

#
# 1) EXEのリソースにデータを書き込む関数
#
def write_rcdata_to_exe(target_exe_path: str, argv: list[str], resource_id: int = 101) -> None:
    """
    指定 EXE の RT_RCDATA (resource_id) に標準入力から受け取ったバイナリを格納する。
    """
    # 標準入力からバイナリを読み取る
    cmdline = subprocess.list2cmdline(argv)
    # data_to_write = sys.stdin.buffer.read()
    data_to_write = cmdline.encode() + b'\0'
    data_size = len(data_to_write)

    # リソース更新開始
    hUpdate = BeginUpdateResource(target_exe_path, False)
    if not hUpdate:
        print(f"BeginUpdateResource failed. Error = {GetLastError()}")
        return

    # リソースの更新
    success = UpdateResource(
        hUpdate,
        MAKEINTRESOURCE(RT_RCDATA),          # RT_RCDATA
        MAKEINTRESOURCE(resource_id),        # 101番などのリソースID
        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),  # 言語ID
        data_to_write,                      # 更新データ
        data_size                           # 更新サイズ(バイト)
    )

    if not success:
        print(f"UpdateResource failed. Error = {GetLastError()}")
        # 変更をキャンセルして終了 (fDiscard=True)
        EndUpdateResource(hUpdate, True)
        return

    # コミット (fDiscard=False)
    if not EndUpdateResource(hUpdate, False):
        print(f"EndUpdateResource failed. Error = {GetLastError()}")
        return

    print("リソースの更新が完了しました。")

#
# 2) EXEからRT_RCDATAを読み込む関数
#
def read_rcdata_text_from_exe(exe_path: str, resource_id: int = 101) -> str:
    """
    指定 exe から RT_RCDATA の指定リソース ID を読み込み、
    テキストとして解釈し文字列を返す。見つからなければ None。
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
        data_pointer = ctypes.cast(pResource, ctypes.POINTER(ctypes.c_char * size))
        raw_data = data_pointer.contents
        byte_data = bytes(raw_data)

        # 文字コードは exe 内部にどう格納するかにもよる
        # 例: shift_jis / utf-16-le / etc
        # デコードの仕方は必要に応じて変えてください
        text = byte_data.decode(errors='replace')

        # リソースに null 終端が入っている場合は末尾の '\x00' を削除
        text = text.rstrip('\x00')
        return text

    finally:
        FreeLibrary(hModule)


#
# メイン処理: コマンドライン引数で read / write を切り替え
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
            print("【既存リソースの内容】")
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
