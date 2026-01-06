import sys
import ctypes
import subprocess
import os
import base64
import gzip
import ctypes.wintypes

#
# Shared constants / functions / WinAPI definitions
#
RT_RCDATA = 10
LOAD_LIBRARY_AS_DATAFILE = 0x00000002

LANG_NEUTRAL = 0x00
SUBLANG_NEUTRAL = 0x00

def MAKELANGID(primary, sublang):
    return (sublang << 10) | primary

def MAKEINTRESOURCE(i):
    return ctypes.cast(ctypes.c_void_p(i), ctypes.wintypes.LPCWSTR)

# -- WinAPI used for writing (updating) resources ---
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

# -- WinAPI used for reading resources ---
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

# Get last error code
GetLastError = ctypes.windll.kernel32.GetLastError
GetLastError.restype = ctypes.wintypes.DWORD

#
# 1) Function to write data into an EXE resource
#
def write_rcdata_to_exe(target_exe_path: str, argv: list[str], resource_id: int = 101) -> None:
    """
    Store binary data received from standard input into the specified EXE's
    RT_RCDATA resource (resource_id).
    """
    # Read binary data from standard input
    cmdline = subprocess.list2cmdline(argv)
    data_to_write = cmdline.encode("utf-16-le")
    data_size = len(data_to_write)

    # Begin resource update
    hUpdate = BeginUpdateResource(target_exe_path, False)
    if not hUpdate:
        print(f"BeginUpdateResource failed. Error = {GetLastError()}")
        return

    # Update the resource
    success = UpdateResource(
        hUpdate,
        MAKEINTRESOURCE(RT_RCDATA),          # RT_RCDATA
        MAKEINTRESOURCE(resource_id),        # Resource ID such as 101
        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),  # Language ID
        data_to_write,                      # Data to write
        data_size                           # Size of data (bytes)
    )

    if not success:
        print(f"UpdateResource failed. Error = {GetLastError()}")
        # Cancel changes and exit (fDiscard=True)
        EndUpdateResource(hUpdate, True)
        return

    # Commit changes (fDiscard=False)
    if not EndUpdateResource(hUpdate, False):
        print(f"EndUpdateResource failed. Error = {GetLastError()}")
        return

    # print("Resource update completed.")

#
# 2) Function to read RT_RCDATA from an EXE
#
def read_rcdata_text_from_exe(exe_path: str, resource_id: int = 101) -> str:
    """
    Read the specified RT_RCDATA resource ID from the given EXE,
    interpret it as text, and return it as a string.
    Returns None if not found.
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

        # Extract pResource as binary data
        data_pointer = ctypes.cast(pResource, ctypes.POINTER(ctypes.c_char * size))
        raw_data = data_pointer.contents
        byte_data = bytes(raw_data)

        # Character encoding depends on how data is stored in the EXE
        # Examples: shift_jis / utf-16-le / etc.
        # Adjust decoding as needed
        text = byte_data.decode("utf-16-le")

        # Remove trailing '\x00' if the resource contains a null terminator
        text = text.rstrip('\x00')
        return text

    finally:
        FreeLibrary(hModule)


#
# Main logic: switch between read / write via command-line arguments
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
            print("RT_RCDATA (ID=101) does not exist or failed to read.")
        else:
            # print("[Existing resource content]")
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
        print(f"Unknown command: {mode}")
        print("Please specify 'read' or 'write'.")
        sys.exit(1)

exelink_bin = gzip.decompress(base64.b64decode("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"))

if __name__ == "__main__":
    main()
