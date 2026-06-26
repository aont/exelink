#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CONFIG_RESOURCE_ID 101
#define MAX_CMDLINE_WCHARS 32767
#define MAX_ENV_KEY_WCHARS 256
#define MAX_ENV_VAL_WCHARS 32767

static LPCWSTR ShiftCommandLine(LPCWSTR s)
{
    BOOL quoted = FALSE, slash = FALSE, done = FALSE;
    for (;; s++)
    {
        WCHAR c = *s;
        if (done)
        {
            if (c != L' ' && c != L'\t')
                return s;
            continue;
        }
        if (!c)
            return s;
        if (c == L'\\')
            slash = !slash;
        else if (c == L'"' && !slash)
            quoted = !quoted;
        else
        {
            if (!quoted && (c == L' ' || c == L'\t'))
                done = TRUE;
            slash = FALSE;
        }
    }
}

static int LoadConfig(LPVOID *data, DWORD *size)
{
    HRSRC ri = FindResourceW(NULL, MAKEINTRESOURCEW(CONFIG_RESOURCE_ID), MAKEINTRESOURCEW(RT_RCDATA));
    HGLOBAL rd = ri ? LoadResource(NULL, ri) : NULL;
    *size = ri ? SizeofResource(NULL, ri) : 0;
    *data = rd && *size ? LockResource(rd) : NULL;
    return *data ? 0 : -1;
}

static ULONGLONG ReadU64LE(const BYTE *p)
{
    ULONGLONG v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

static int ReadBlob(const BYTE *p, DWORD size, const BYTE **argv, DWORD *argv_bytes)
{
    static const BYTE ARGV[8] = {'A', 0, 'R', 0, 'G', 0, 'V', 0};
    static const BYTE ENV[8] = {'E', 0, 'N', 0, 'V', 0, 0, 0};
    static const BYTE END[8] = {'E', 0, 'N', 0, 'D', 0, 0, 0};
    ULONGLONG left = size;
    BOOL got_argv = FALSE;

    *argv = NULL;
    *argv_bytes = 0;
    while (left >= 8)
    {
        const BYTE *type = p;
        p += 8;
        left -= 8;
        if (!memcmp(type, END, 8))
            return got_argv ? 0 : -1;
        if (!memcmp(type, ARGV, 8))
        {
            if (left < 8)
                return -1;
            ULONGLONG len = ReadU64LE(p);
            p += 8;
            left -= 8;
            if (len > left || (len & 1) || !len || len / 2 > MAX_CMDLINE_WCHARS)
                return -1;
            *argv = p;
            *argv_bytes = (DWORD)len;
            got_argv = TRUE;
            p += len;
            left -= len;
            continue;
        }
        if (!memcmp(type, ENV, 8))
        {
            if (left < 8)
                return -1;
            ULONGLONG klen = ReadU64LE(p);
            p += 8;
            left -= 8;
            if (klen > left || (klen & 1) || !klen || klen / 2 >= MAX_ENV_KEY_WCHARS)
                return -1;
            WCHAR key[MAX_ENV_KEY_WCHARS];
            memcpy(key, p, (size_t)klen);
            key[klen / 2] = 0;
            p += klen;
            left -= klen;

            if (left < 8)
                return -1;
            ULONGLONG vlen = ReadU64LE(p);
            p += 8;
            left -= 8;
            if (vlen > left || (vlen & 1) || vlen / 2 > MAX_ENV_VAL_WCHARS)
                return -1;
            LPWSTR value = (LPWSTR)calloc((size_t)vlen / 2 + 1, sizeof(WCHAR));
            if (!value)
                return -1;
            memcpy(value, p, (size_t)vlen);
            SetEnvironmentVariableW(key, value);
            free(value);
            p += vlen;
            left -= vlen;
            continue;
        }
        return -1;
    }
    return -1;
}

int main(void)
{
    LPVOID config;
    DWORD config_size, prefix_bytes, code = 1;
    const BYTE *prefix;

    if (LoadConfig(&config, &config_size) || ReadBlob((const BYTE *)config, config_size, &prefix, &prefix_bytes))
    {
        fwprintf(stderr, L"invalid exelink config resource %u\n", CONFIG_RESOURCE_ID);
        return 1;
    }

    LPCWSTR extra = ShiftCommandLine(GetCommandLineW());
    size_t prefix_chars = prefix_bytes / sizeof(WCHAR), extra_chars = wcslen(extra);
    LPWSTR cmd = (LPWSTR)calloc(prefix_chars + extra_chars + 2, sizeof(WCHAR));
    if (!cmd)
        return 1;
    memcpy(cmd, prefix, prefix_bytes);
    cmd[prefix_chars] = L' ';
    memcpy(cmd + prefix_chars + 1, extra, extra_chars * sizeof(WCHAR));

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        fwprintf(stderr, L"CreateProcess failed. ErrorCode=%lu\n", GetLastError());
        free(cmd);
        return 1;
    }
    free(cmd);

    SetConsoleCtrlHandler(NULL, TRUE);
    WaitForSingleObject(pi.hProcess, INFINITE);
    SetConsoleCtrlHandler(NULL, FALSE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}
