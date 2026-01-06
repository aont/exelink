#include <windows.h>

//----------------------------------------------------------
// Minimal memcpy / memset implementations to eliminate CRT
//----------------------------------------------------------
#pragma function(memcpy)
#pragma function(memset)

void* __cdecl memcpy(void* dst, const void* src, size_t n)
{
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void* __cdecl memset(void* dst, int c, size_t n)
{
    unsigned char* d = (unsigned char*)dst;
    unsigned char v = (unsigned char)c;
    while (n--) {
        *d++ = v;
    }
    return dst;
}

//----------------------------------------------------------
// Utility to output a wide string to standard error
//----------------------------------------------------------
static VOID OutputErrorMessageW(LPCWSTR pMessage)
{
    HANDLE hError = GetStdHandle(STD_ERROR_HANDLE);
    if (hError == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(hError, pMessage, lstrlenW(pMessage) * sizeof(WCHAR), &written, NULL);
}

//----------------------------------------------------------
// Utility to output an error message with an error code
//  Note: wsprintfW is in user32.dll and requires user32.lib
//----------------------------------------------------------
static VOID OutputErrorMessageWithCode(LPCWSTR pMessage, DWORD errorCode)
{
    WCHAR buffer[256];
    wsprintfW(buffer, L"%s (ErrorCode=%lu)\r\n", pMessage, errorCode);
    OutputErrorMessageW(buffer);
}

//----------------------------------------------------------
// Skip argv[0] and return the remaining command line
//----------------------------------------------------------
static LPCWSTR ShiftCommandLine(LPCWSTR pCmdLine)
{
    BOOL isBackslashPreceding = FALSE;
    BOOL isInsideDoubleQuote  = FALSE;
    BOOL isAfterArgv0         = FALSE;

    for (int i = 0;; i++) {
        const WCHAR ch = pCmdLine[i];
        if (isAfterArgv0) {
            switch (ch) {
            case L' ':
            case L'\t':
                break;
            default:
                return pCmdLine + i;
            }
        } else if (isInsideDoubleQuote) {
            switch (ch) {
            case L'\\':
                isBackslashPreceding = !isBackslashPreceding;
                break;
            case L'"':
                if (!isBackslashPreceding) {
                    isInsideDoubleQuote = FALSE;
                } else {
                    isBackslashPreceding = FALSE;
                }
                break;
            case L'\0':
                return pCmdLine + i;
            default:
                isBackslashPreceding = FALSE;
                break;
            }
        } else {
            switch (ch) {
            case L'\\':
                isBackslashPreceding = !isBackslashPreceding;
                break;
            case L'"':
                if (!isBackslashPreceding) {
                    isInsideDoubleQuote = TRUE;
                } else {
                    isBackslashPreceding = FALSE;
                }
                break;
            case L' ':
            case L'\t':
                isAfterArgv0 = TRUE;
                break;
            case L'\0':
                return pCmdLine + i;
            default:
                isBackslashPreceding = FALSE;
                break;
            }
        }
    }
}

//----------------------------------------------------------
// Load an embedded resource
//----------------------------------------------------------
static INT LoadEmbeddedResource(INT resourceId, LPVOID* ppResourceData, DWORD* pResourceSize)
{
    HRSRC hResourceInfo = FindResourceW(NULL, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(RT_RCDATA));
    if (!hResourceInfo) {
        OutputErrorMessageW(L"Failed to find resource.\r\n");
        return -1;
    }

    HGLOBAL hResourceData = LoadResource(NULL, hResourceInfo);
    if (!hResourceData) {
        OutputErrorMessageW(L"Failed to load resource.\r\n");
        return -1;
    }

    DWORD resourceSize = SizeofResource(NULL, hResourceInfo);
    if (resourceSize == 0) {
        OutputErrorMessageW(L"Resource size is zero.\r\n");
        return -1;
    }
    *pResourceSize = resourceSize;

    LPVOID pLockRes = LockResource(hResourceData);
    if (!pLockRes) {
        OutputErrorMessageW(L"Failed to lock resource.\r\n");
        return -1;
    }
    *ppResourceData = pLockRes;

    return 0;
}

//----------------------------------------------------------
// Entry point
//----------------------------------------------------------
int mainCRTStartup(void)
{
    LPVOID pResource = NULL;
    DWORD prefixLength = 0;
    if (LoadEmbeddedResource(101, &pResource, &prefixLength) != 0) {
        OutputErrorMessageW(L"LoadEmbeddedResource failed.\r\n");
        return -1;
    }

    LPCWSTR pCmdLine = GetCommandLineW();
    LPCWSTR pShiftedCmdLine = ShiftCommandLine(pCmdLine);
    SIZE_T shiftedCmdLineLength = (SIZE_T)lstrlenW(pShiftedCmdLine);

    // prefix (UTF-16) + space + shifted command line + NUL
    SIZE_T newCmdLineSize =
        (SIZE_T)prefixLength + sizeof(WCHAR) + shiftedCmdLineLength * sizeof(WCHAR) + sizeof(WCHAR);

    HANDLE hProcessHeap = GetProcessHeap();
    LPWSTR pNewCmdLineBuffer = (LPWSTR)HeapAlloc(hProcessHeap, HEAP_ZERO_MEMORY, newCmdLineSize);
    if (!pNewCmdLineBuffer) {
        OutputErrorMessageW(L"HeapAlloc failed.\r\n");
        return -1;
    }

    // Using CopyMemory here may introduce a reference to memcpy due to optimization,
    // so a custom memcpy implementation is used to ensure reliable resolution.
    memcpy(pNewCmdLineBuffer, pResource, prefixLength);
    pNewCmdLineBuffer[prefixLength / sizeof(WCHAR)] = L' ';
    memcpy(
        pNewCmdLineBuffer + (prefixLength / sizeof(WCHAR)) + 1,
        pShiftedCmdLine,
        shiftedCmdLineLength * sizeof(WCHAR)
    );
    pNewCmdLineBuffer[(prefixLength / sizeof(WCHAR)) + 1 + shiftedCmdLineLength] = L'\0';

    STARTUPINFOW startupInfo;
    // ZeroMemory may also introduce a memset reference due to optimization,
    // so a custom memset implementation is used instead.
    memset(&startupInfo, 0, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo;
    memset(&processInfo, 0, sizeof(processInfo));

    BOOL isSuccess = CreateProcessW(
        NULL,
        pNewCmdLineBuffer,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &startupInfo,
        &processInfo
    );

    HeapFree(hProcessHeap, 0, pNewCmdLineBuffer);

    if (!isSuccess) {
        DWORD lastError = GetLastError();
        OutputErrorMessageWithCode(L"CreateProcess failed.", lastError);
        return -1;
    }

    SetConsoleCtrlHandler(NULL, TRUE);

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    SetConsoleCtrlHandler(NULL, FALSE);

    DWORD exitCode = 0;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);

    ExitProcess(exitCode);
    return (int)exitCode;
}
