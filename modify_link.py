import ctypes
from ctypes import wintypes
import sys

# --- 定数定義 ---
RT_RCDATA = 10  # WinUser.h で #define RT_RCDATA MAKEINTRESOURCE(10) と定義されているものに対応

LANG_NEUTRAL = 0x00
SUBLANG_NEUTRAL = 0x00

def MAKELANGID(primary, sublang):
    return (sublang << 10) | primary

def MAKEINTRESOURCE(i):
    # リソース名/ID を (LPWSTR) キャストしたポインタとして扱うためのユーティリティ
    return ctypes.cast(ctypes.c_void_p(i), wintypes.LPCWSTR)

# --- 関数定義 (WinAPI) ---
# BOOL WINAPI UpdateResource(
#   _In_ HANDLE hUpdate,
#   _In_ LPCTSTR lpType,
#   _In_ LPCTSTR lpName,
#   _In_ WORD    wLanguage,
#   _In_ LPVOID  lpData,
#   _In_ DWORD   cbData
# );
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

# HANDLE WINAPI BeginUpdateResource(
#   _In_  LPCTSTR pFileName,
#   _In_  BOOL    bDeleteExistingResources
# );
BeginUpdateResource = ctypes.windll.kernel32.BeginUpdateResourceW
BeginUpdateResource.argtypes = (
    wintypes.LPCWSTR,
    wintypes.BOOL
)
BeginUpdateResource.restype = wintypes.HANDLE

# BOOL WINAPI EndUpdateResource(
#   _In_ HANDLE hUpdate,
#   _In_ BOOL   fDiscard
# );
EndUpdateResource = ctypes.windll.kernel32.EndUpdateResourceW
EndUpdateResource.argtypes = (
    wintypes.HANDLE,
    wintypes.BOOL
)
EndUpdateResource.restype = wintypes.BOOL

# GetLastError 用
GetLastError = ctypes.windll.kernel32.GetLastError
GetLastError.restype = wintypes.DWORD

def update_resource_example(targetExePath: str):
    # 更新対象の exe パス
    # targetExePath = r"C:\path\to\example.exe"

    # 新しい文字列リソース
    # newText = "こんにちは、新しいテキストリソースです！"

    # WideChar 用のバッファ作成 (null終端を含めた長さで確保)
    # text_buffer = ctypes.create_string_buffer(newText.encode('shift_jis')) # + b'\0')
    # text_buffer = newText.encode('shift_jis')
    text_buffer = sys.stdin.buffer.read()
    # バッファサイズ(バイト)
    # text_size = (len(text_buffer) * ctypes.sizeof(ctypes.c_char))
    text_size = len(text_buffer)

    # リソース更新開始
    hUpdate = BeginUpdateResource(targetExePath, False)
    if not hUpdate:
        print(f"BeginUpdateResource failed. Error = {GetLastError()}")
        return

    # リソースの更新
    success = UpdateResource(
        hUpdate,
        MAKEINTRESOURCE(RT_RCDATA),          # RT_RCDATA
        MAKEINTRESOURCE(101),                # 101 番リソース
        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),  # 言語ID
        text_buffer,                         # 更新データ
        text_size                            # 更新サイズ(バイト)
    )

    if not success:
        print(f"UpdateResource failed. Error = {GetLastError()}")
        # 変更をキャンセルして終了
        EndUpdateResource(hUpdate, True)
        return

    # リソース書き込みの確定 (False = コミット)
    if not EndUpdateResource(hUpdate, False):
        print(f"EndUpdateResource failed. Error = {GetLastError()}")
        return

    print("リソースの更新が完了しました。")

if __name__ == "__main__":
    update_resource_example(sys.argv[1])
