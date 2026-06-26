#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static LPCWSTR ShiftCommandLine(LPCWSTR s)
{
    BOOL slash = FALSE, quote = FALSE, after = FALSE;
    for (int i = 0;; i++)
    {
        WCHAR ch = s[i];
        if (after)
        {
            if (ch != L' ' && ch != L'\t')
                return s + i;
            continue;
        }
        if (quote)
        {
            if (ch == L'\\')
                slash = !slash;
            else if (ch == L'"')
                quote = slash ? quote : FALSE, slash = FALSE;
            else if (ch == L'\0')
                return s + i;
            else
                slash = FALSE;
            continue;
        }
        if (ch == L'\\')
            slash = !slash;
        else if (ch == L'"')
            quote = slash ? quote : TRUE, slash = FALSE;
        else if (ch == L' ' || ch == L'\t')
            after = TRUE;
        else if (ch == L'\0')
            return s + i;
        else
            slash = FALSE;
    }
}

static BOOL LoadEmbeddedResource(INT id, const BYTE **data, DWORD *size)
{
    HRSRC info = FindResourceW(NULL, MAKEINTRESOURCEW(id), RT_RCDATA);
    HGLOBAL res = info ? LoadResource(NULL, info) : NULL;
    *size = info ? SizeofResource(NULL, info) : 0;
    *data = res ? (const BYTE *)LockResource(res) : NULL;
    return *data && *size;
}

#define MAX_CMDLINE_WCHARS 32767
#define MAX_ENV_KEY_WCHARS 256
#define MAX_ENV_VAL_WCHARS 32767

static uint64_t ReadU64LE(const BYTE *p)
{
    uint64_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static INT ParseConfigBlobApplyEnvAndGetArgvPrefix(
    const BYTE *p,
    DWORD dataSize,
    const BYTE **argvPrefix,
    DWORD *argvPrefixBytes)
{
    static const BYTE TYPE_ARGV[8] = {'A', 0, 'R', 0, 'G', 0, 'V', 0};
    static const BYTE TYPE_ENV[8] = {'E', 0, 'N', 0, 'V', 0, 0, 0};
    static const BYTE TYPE_END[8] = {'E', 0, 'N', 0, 'D', 0, 0, 0};

    uint64_t remaining = dataSize;
    BOOL hasArgv = FALSE;
    *argvPrefix = NULL;
    *argvPrefixBytes = 0;

#define NEED(n)        \
    do                 \
    {                  \
        if ((n) > remaining) \
            return -1; \
    } while (0)
#define READ_LEN(var)  \
    NEED(8);           \
    var = ReadU64LE(p); \
    p += 8;            \
    remaining -= 8

    while (remaining >= 8)
    {
        const BYTE *type = p;
        p += 8;
        remaining -= 8;

        if (!memcmp(type, TYPE_END, 8))
            return hasArgv ? 0 : -1;

        if (!memcmp(type, TYPE_ARGV, 8))
        {
            uint64_t len;
            READ_LEN(len);
            NEED(len);
            if ((len & 1) || len == 0 || len / 2 > MAX_CMDLINE_WCHARS)
                return -1;
            *argvPrefix = p;
            *argvPrefixBytes = (DWORD)len;
            hasArgv = TRUE;
            p += (size_t)len;
            remaining -= len;
            continue;
        }

        if (!memcmp(type, TYPE_ENV, 8))
        {
            uint64_t klen, vlen;
            READ_LEN(klen);
            NEED(klen);
            if ((klen & 1) || klen == 0 || klen / 2 >= MAX_ENV_KEY_WCHARS)
                return -1;

            WCHAR key[MAX_ENV_KEY_WCHARS];
            memcpy(key, p, (size_t)klen);
            key[klen / 2] = L'\0';
            p += (size_t)klen;
            remaining -= klen;

            READ_LEN(vlen);
            NEED(vlen);
            if ((vlen & 1) || vlen / 2 > MAX_ENV_VAL_WCHARS)
                return -1;

            WCHAR *value = calloc((size_t)vlen / sizeof(WCHAR) + 1, sizeof(WCHAR));
            if (!value)
                return -1;
            memcpy(value, p, (size_t)vlen);
            SetEnvironmentVariableW(key, value);
            free(value);

            p += (size_t)vlen;
            remaining -= vlen;
            continue;
        }
        return -1;
    }
    return -1;
}

int wmain(void)
{
    const BYTE *config = NULL, *prefix = NULL;
    DWORD configSize = 0, prefixBytes = 0;

    if (!LoadEmbeddedResource(101, &config, &configSize))
        return fwprintf(stderr, L"LoadEmbeddedResource(101) failed.\n"), -1;

    if (ParseConfigBlobApplyEnvAndGetArgvPrefix(config, configSize, &prefix, &prefixBytes))
        return fwprintf(stderr, L"ParseConfigBlob failed (invalid config blob).\n"), -1;

    LPCWSTR args = ShiftCommandLine(GetCommandLineW());
    size_t prefixChars = prefixBytes / sizeof(WCHAR);
    size_t argsChars = wcslen(args);
    WCHAR *cmd = calloc(prefixChars + 1 + argsChars + 1, sizeof(WCHAR));
    if (!cmd)
        return fwprintf(stderr, L"calloc failed.\n"), -1;

    memcpy(cmd, prefix, prefixBytes);
    cmd[prefixChars] = L' ';
    wcscpy(cmd + prefixChars + 1, args);

    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {0};
    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    free(cmd);

    if (!ok)
        return fwprintf(stderr, L"CreateProcess failed. (ErrorCode=%lu)\n", GetLastError()), -1;

    SetConsoleCtrlHandler(NULL, TRUE);
    WaitForSingleObject(pi.hProcess, INFINITE);
    SetConsoleCtrlHandler(NULL, FALSE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exitCode;
}
