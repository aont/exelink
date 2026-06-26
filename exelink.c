#include <windows.h>

//----------------------------------------------------------
// Minimal memcpy / memset implementations to eliminate CRT
//----------------------------------------------------------
#pragma function(memcpy)
#pragma function(memset)

void *__cdecl memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
    {
        *d++ = *s++;
    }
    return dst;
}

void *__cdecl memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    unsigned char v = (unsigned char)c;
    while (n--)
    {
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
    if (hError == INVALID_HANDLE_VALUE)
    {
        return;
    }
    DWORD written = 0;
    WriteFile(hError, pMessage, lstrlenW(pMessage) * sizeof(WCHAR), &written, NULL);
}

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
    BOOL isInsideDoubleQuote = FALSE;
    BOOL isAfterArgv0 = FALSE;

    for (int i = 0;; i++)
    {
        const WCHAR ch = pCmdLine[i];
        if (isAfterArgv0)
        {
            switch (ch)
            {
            case L' ':
            case L'\t':
                break;
            default:
                return pCmdLine + i;
            }
        }
        else if (isInsideDoubleQuote)
        {
            switch (ch)
            {
            case L'\\':
                isBackslashPreceding = !isBackslashPreceding;
                break;
            case L'"':
                if (!isBackslashPreceding)
                {
                    isInsideDoubleQuote = FALSE;
                }
                else
                {
                    isBackslashPreceding = FALSE;
                }
                break;
            case L'\0':
                return pCmdLine + i;
            default:
                isBackslashPreceding = FALSE;
                break;
            }
        }
        else
        {
            switch (ch)
            {
            case L'\\':
                isBackslashPreceding = !isBackslashPreceding;
                break;
            case L'"':
                if (!isBackslashPreceding)
                {
                    isInsideDoubleQuote = TRUE;
                }
                else
                {
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
static INT LoadEmbeddedResource(INT resourceId, LPVOID *ppResourceData, DWORD *pResourceSize)
{
    HRSRC hResourceInfo = FindResourceW(NULL, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(RT_RCDATA));
    if (!hResourceInfo)
        return -1;

    HGLOBAL hResourceData = LoadResource(NULL, hResourceInfo);
    if (!hResourceData)
        return -1;

    DWORD resourceSize = SizeofResource(NULL, hResourceInfo);
    if (resourceSize == 0)
        return -1;

    LPVOID pLockRes = LockResource(hResourceData);
    if (!pLockRes)
        return -1;

    *ppResourceData = pLockRes;
    *pResourceSize = resourceSize;
    return 0;
}

//----------------------------------------------------------
// Config blob helpers (type is UTF-16LE 4-WCHAR == 8 bytes)
//----------------------------------------------------------
static ULONGLONG ReadU64LE(const BYTE *p)
{
    return ((ULONGLONG)p[0]) |
           ((ULONGLONG)p[1] << 8) |
           ((ULONGLONG)p[2] << 16) |
           ((ULONGLONG)p[3] << 24) |
           ((ULONGLONG)p[4] << 32) |
           ((ULONGLONG)p[5] << 40) |
           ((ULONGLONG)p[6] << 48) |
           ((ULONGLONG)p[7] << 56);
}

static BOOL TypeEquals8(const BYTE *p, const BYTE t[8])
{
    for (int i = 0; i < 8; i++)
    {
        if (p[i] != t[i])
            return FALSE;
    }
    return TRUE;
}

#define MAX_CMDLINE_WCHARS 32767
#define MAX_ENV_KEY_WCHARS 256
#define MAX_ENV_VAL_WCHARS 32767

static INT ParseConfigBlobApplyEnvAndGetArgvPrefix(
    const BYTE *pData,
    DWORD dataSize,
    const BYTE **ppArgvPrefixBytes,
    DWORD *pArgvPrefixByteLen)
{
    // UTF-16LE, 4 WCHAR each:
    // "ARGV" -> A\0 R\0 G\0 V\0
    // "ENV\0" -> E\0 N\0 V\0 \0\0
    // "END\0" -> E\0 N\0 D\0 \0\0
    static const BYTE TYPE_ARGV[8] = {'A', 0, 'R', 0, 'G', 0, 'V', 0};
    static const BYTE TYPE_ENV[8] = {'E', 0, 'N', 0, 'V', 0, 0, 0};
    static const BYTE TYPE_END[8] = {'E', 0, 'N', 0, 'D', 0, 0, 0};

    const BYTE *p = pData;
    ULONGLONG remaining = (ULONGLONG)dataSize;

    BOOL hasArgv = FALSE;
    *ppArgvPrefixBytes = NULL;
    *pArgvPrefixByteLen = 0;

    while (remaining >= 8)
    {
        const BYTE *pType = p;
        p += 8;
        remaining -= 8;

        if (TypeEquals8(pType, TYPE_END))
        {
            return hasArgv ? 0 : -1;
        }

        if (TypeEquals8(pType, TYPE_ARGV))
        {
            if (remaining < 8)
                return -1;
            ULONGLONG len = ReadU64LE(p);
            p += 8;
            remaining -= 8;

            if (len > remaining)
                return -1;
            if ((len % 2) != 0)
                return -1; // UTF-16LE bytes

            ULONGLONG wchars = len / 2;
            if (wchars == 0 || wchars > MAX_CMDLINE_WCHARS)
                return -1;

            *ppArgvPrefixBytes = p;
            *pArgvPrefixByteLen = (DWORD)len;
            hasArgv = TRUE;

            p += (SIZE_T)len;
            remaining -= len;
            continue;
        }

        if (TypeEquals8(pType, TYPE_ENV))
        {
            if (remaining < 8)
                return -1;
            ULONGLONG klen = ReadU64LE(p);
            p += 8;
            remaining -= 8;

            if (klen > remaining)
                return -1;
            if ((klen % 2) != 0)
                return -1;

            ULONGLONG kchars = klen / 2;
            if (kchars == 0 || kchars >= MAX_ENV_KEY_WCHARS)
                return -1;

            WCHAR keyBuf[MAX_ENV_KEY_WCHARS];
            memcpy(keyBuf, p, (SIZE_T)klen);
            keyBuf[(SIZE_T)kchars] = L'\0';

            p += (SIZE_T)klen;
            remaining -= klen;

            if (remaining < 8)
                return -1;
            ULONGLONG vlen = ReadU64LE(p);
            p += 8;
            remaining -= 8;

            if (vlen > remaining)
                return -1;
            if ((vlen % 2) != 0)
                return -1;

            // vlen==0 => set empty string (NOT unset)
            if (vlen == 0)
            {
                SetEnvironmentVariableW(keyBuf, L"");
            }
            else
            {
                ULONGLONG vchars = vlen / 2;
                if (vchars > MAX_ENV_VAL_WCHARS)
                    return -1;

                HANDLE hHeap = GetProcessHeap();
                SIZE_T bytes = (SIZE_T)vlen + sizeof(WCHAR);
                LPWSTR valueBuf = (LPWSTR)HeapAlloc(hHeap, HEAP_ZERO_MEMORY, bytes);
                if (!valueBuf)
                    return -1;

                memcpy(valueBuf, p, (SIZE_T)vlen);
                valueBuf[(SIZE_T)vchars] = L'\0';

                SetEnvironmentVariableW(keyBuf, valueBuf);
                HeapFree(hHeap, 0, valueBuf);
            }

            p += (SIZE_T)vlen;
            remaining -= vlen;
            continue;
        }

        // Unknown type => fail
        return -1;
    }

    // No END
    return -1;
}

//----------------------------------------------------------
// Entry point
//----------------------------------------------------------
int mainCRTStartup(void)
{
    LPVOID pConfigResource = NULL;
    DWORD configSize = 0;
    if (LoadEmbeddedResource(101, &pConfigResource, &configSize) != 0)
    {
        OutputErrorMessageW(L"LoadEmbeddedResource(101) failed.\r\n");
        return -1;
    }

    const BYTE *pArgvPrefixBytes = NULL;
    DWORD argvPrefixByteLen = 0;

    if (ParseConfigBlobApplyEnvAndGetArgvPrefix(
            (const BYTE *)pConfigResource,
            configSize,
            &pArgvPrefixBytes,
            &argvPrefixByteLen) != 0)
    {
        OutputErrorMessageW(L"ParseConfigBlob failed (invalid config blob).\r\n");
        return -1;
    }

    LPCWSTR pCmdLine = GetCommandLineW();
    LPCWSTR pShiftedCmdLine = ShiftCommandLine(pCmdLine);
    SIZE_T shiftedCmdLineLength = (SIZE_T)lstrlenW(pShiftedCmdLine);

    SIZE_T newCmdLineSize =
        (SIZE_T)argvPrefixByteLen + sizeof(WCHAR) + shiftedCmdLineLength * sizeof(WCHAR) + sizeof(WCHAR);

    HANDLE hProcessHeap = GetProcessHeap();
    LPWSTR pNewCmdLineBuffer = (LPWSTR)HeapAlloc(hProcessHeap, HEAP_ZERO_MEMORY, newCmdLineSize);
    if (!pNewCmdLineBuffer)
    {
        OutputErrorMessageW(L"HeapAlloc failed.\r\n");
        return -1;
    }

    memcpy(pNewCmdLineBuffer, pArgvPrefixBytes, argvPrefixByteLen);
    pNewCmdLineBuffer[argvPrefixByteLen / sizeof(WCHAR)] = L' ';
    memcpy(
        pNewCmdLineBuffer + (argvPrefixByteLen / sizeof(WCHAR)) + 1,
        pShiftedCmdLine,
        shiftedCmdLineLength * sizeof(WCHAR));
    pNewCmdLineBuffer[(argvPrefixByteLen / sizeof(WCHAR)) + 1 + shiftedCmdLineLength] = L'\0';

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
        &processInfo);

    HeapFree(hProcessHeap, 0, pNewCmdLineBuffer);

    if (!isSuccess)
    {
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
