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
// Load an embedded resource (RT_RCDATA, given id)
//----------------------------------------------------------
static INT LoadEmbeddedResource(INT resourceId, LPVOID* ppResourceData, DWORD* pResourceSize)
{
    HRSRC hResourceInfo = FindResourceW(NULL, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(RT_RCDATA));
    if (!hResourceInfo) {
        return -1;
    }

    HGLOBAL hResourceData = LoadResource(NULL, hResourceInfo);
    if (!hResourceData) {
        return -1;
    }

    DWORD resourceSize = SizeofResource(NULL, hResourceInfo);
    if (resourceSize == 0) {
        return -1;
    }
    *pResourceSize = resourceSize;

    LPVOID pLockRes = LockResource(hResourceData);
    if (!pLockRes) {
        return -1;
    }
    *ppResourceData = pLockRes;

    return 0;
}

//----------------------------------------------------------
// Apply environment variables from UTF-16 text resource.
// Format:
//   - Lines separated by \n or \r\n
//   - Ignore empty lines and lines starting with '#'
//   - "NAME=VALUE" sets variable
//   - "NAME" (no '=') unsets variable
//----------------------------------------------------------
static VOID ApplyEnvironmentFromResource(LPCWSTR pText, SIZE_T cchText)
{
    SIZE_T i = 0;

    while (i < cchText) {
        // Skip leading CR/LF
        while (i < cchText && (pText[i] == L'\r' || pText[i] == L'\n')) {
            i++;
        }
        if (i >= cchText) {
            break;
        }

        // Line start
        SIZE_T lineStart = i;

        // Find line end
        while (i < cchText && pText[i] != L'\r' && pText[i] != L'\n') {
            i++;
        }
        SIZE_T lineEnd = i;

        // Trim trailing spaces/tabs (optional; keep minimal)
        while (lineEnd > lineStart && (pText[lineEnd - 1] == L' ' || pText[lineEnd - 1] == L'\t')) {
            lineEnd--;
        }

        // Trim leading spaces/tabs
        while (lineStart < lineEnd && (pText[lineStart] == L' ' || pText[lineStart] == L'\t')) {
            lineStart++;
        }

        if (lineStart >= lineEnd) {
            continue;
        }

        // Comment
        if (pText[lineStart] == L'#') {
            continue;
        }

        // Find '='
        SIZE_T eq = lineStart;
        while (eq < lineEnd && pText[eq] != L'=') {
            eq++;
        }

        if (eq == lineEnd) {
            // No '=' => unset variable
            // Name is [lineStart, lineEnd)
            WCHAR nameBuf[256];
            SIZE_T nameLen = lineEnd - lineStart;
            if (nameLen >= (sizeof(nameBuf) / sizeof(nameBuf[0]))) {
                // too long; ignore
                continue;
            }
            memcpy(nameBuf, pText + lineStart, nameLen * sizeof(WCHAR));
            nameBuf[nameLen] = L'\0';
            SetEnvironmentVariableW(nameBuf, NULL);
        } else {
            // Has '=' => set variable (value may be empty)
            WCHAR nameBuf[256];
            SIZE_T nameLen = eq - lineStart;
            if (nameLen == 0 || nameLen >= (sizeof(nameBuf) / sizeof(nameBuf[0]))) {
                continue;
            }
            memcpy(nameBuf, pText + lineStart, nameLen * sizeof(WCHAR));
            nameBuf[nameLen] = L'\0';

            // Value
            LPCWSTR valuePtr = pText + (eq + 1);
            SIZE_T valueLen = lineEnd - (eq + 1);

            // For safety, cap value copy into heap buffer (variable length)
            HANDLE hHeap = GetProcessHeap();
            SIZE_T bytes = (valueLen + 1) * sizeof(WCHAR);
            LPWSTR valueBuf = (LPWSTR)HeapAlloc(hHeap, HEAP_ZERO_MEMORY, bytes);
            if (!valueBuf) {
                continue;
            }
            if (valueLen > 0) {
                memcpy(valueBuf, valuePtr, valueLen * sizeof(WCHAR));
            }
            valueBuf[valueLen] = L'\0';

            SetEnvironmentVariableW(nameBuf, valueBuf);
            HeapFree(hHeap, 0, valueBuf);
        }
    }
}

//----------------------------------------------------------
// Entry point
//----------------------------------------------------------
int mainCRTStartup(void)
{
    // --- Load command prefix (required) ---
    LPVOID pPrefixResource = NULL;
    DWORD prefixLength = 0;
    if (LoadEmbeddedResource(101, &pPrefixResource, &prefixLength) != 0) {
        OutputErrorMessageW(L"LoadEmbeddedResource(101) failed.\r\n");
        return -1;
    }

    // --- Load env resource (optional) ---
    LPVOID pEnvResource = NULL;
    DWORD envBytes = 0;
    if (LoadEmbeddedResource(102, &pEnvResource, &envBytes) == 0 && envBytes >= sizeof(WCHAR)) {
        // Apply env vars before CreateProcess (child inherits)
        LPCWSTR pEnvText = (LPCWSTR)pEnvResource;
        SIZE_T cch = (SIZE_T)(envBytes / sizeof(WCHAR));
        // strip possible trailing NULs (optional)
        while (cch > 0 && pEnvText[cch - 1] == L'\0') {
            cch--;
        }
        ApplyEnvironmentFromResource(pEnvText, cch);
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

    memcpy(pNewCmdLineBuffer, pPrefixResource, prefixLength);
    pNewCmdLineBuffer[prefixLength / sizeof(WCHAR)] = L' ';
    memcpy(
        pNewCmdLineBuffer + (prefixLength / sizeof(WCHAR)) + 1,
        pShiftedCmdLine,
        shiftedCmdLineLength * sizeof(WCHAR)
    );
    pNewCmdLineBuffer[(prefixLength / sizeof(WCHAR)) + 1 + shiftedCmdLineLength] = L'\0';

    STARTUPINFOW startupInfo;
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
