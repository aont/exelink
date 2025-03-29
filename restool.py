import sys
import ctypes
import subprocess
import os
import base64
import gzip
import ctypes.wintypes

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
    return ctypes.cast(ctypes.c_void_p(i), ctypes.wintypes.LPCWSTR)

# -- 書き込み(更新)に使用する WinAPI ---
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

# -- 読み込みに使用する WinAPI ---
LoadLibraryExW = ctypes.windll.kernel32.LoadLibraryExW
LoadLibraryExW.argtypes = (ctypes.wintypes.LPCWSTR, ctypes.wintypes.HANDLE, ctypes.wintypes.DWORD)
LoadLibraryExW.restype = ctypes.wintypes.HMODULE

FindResourceW = ctypes.windll.kernel32.FindResourceW
FindResourceW.argtypes = (ctypes.wintypes.HMODULE, ctypes.wintypes.LPCWSTR, ctypes.wintypes.LPCWSTR)
FindResourceW.restype = ctypes.wintypes.HANDLE  # HRSRC

LoadResource = ctypes.windll.kernel32.LoadResource
LoadResource.argtypes = (ctypes.wintypes.HMODULE, ctypes.wintypes.HANDLE)
LoadResource.restype = ctypes.wintypes.HANDLE  # HGLOBAL

LockResource = ctypes.windll.kernel32.LockResource
LockResource.argtypes = (ctypes.wintypes.HANDLE,)
LockResource.restype = ctypes.wintypes.LPVOID

SizeofResource = ctypes.windll.kernel32.SizeofResource
SizeofResource.argtypes = (ctypes.wintypes.HMODULE, ctypes.wintypes.HANDLE)
SizeofResource.restype = ctypes.wintypes.DWORD

FreeLibrary = ctypes.windll.kernel32.FreeLibrary
FreeLibrary.argtypes = (ctypes.wintypes.HMODULE,)
FreeLibrary.restype = ctypes.wintypes.BOOL

# エラーコード取得
GetLastError = ctypes.windll.kernel32.GetLastError
GetLastError.restype = ctypes.wintypes.DWORD

#
# 1) EXEのリソースにデータを書き込む関数
#
def write_rcdata_to_exe(target_exe_path: str, argv: list[str], resource_id: int = 101) -> None:
    """
    指定 EXE の RT_RCDATA (resource_id) に標準入力から受け取ったバイナリを格納する。
    """
    # 標準入力からバイナリを読み取る
    cmdline = subprocess.list2cmdline(argv)
    data_to_write = cmdline.encode("utf-16-le")
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

    # print("リソースの更新が完了しました。")

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
        text = byte_data.decode("utf-16-le")

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
    exe_path = os.path.abspath(sys.argv[2])

    if mode == "read":
        current_text = read_rcdata_text_from_exe(exe_path, resource_id=101)
        if current_text is None:
            print("RT_RCDATA(ID=101) が存在しないか、読み込みに失敗しました。")
        else:
            # print("【既存リソースの内容】")
            print(current_text)

    elif mode == "write":
        argv_prefix = sys.argv[3:]
        write_rcdata_to_exe(exe_path, argv_prefix, resource_id=101)

    elif mode == "create":
        argv_prefix = sys.argv[3:]
        with open(exe_path, "wb") as f:
            f.write(exelink_bin)
        write_rcdata_to_exe(exe_path, argv_prefix, resource_id=101)

    else:
        print(f"不明なコマンド: {mode}")
        print("read または write を指定してください。")
        sys.exit(1)

exelink_bin = gzip.decompress(base64.b64decode("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"))

if __name__ == "__main__":
    main()
