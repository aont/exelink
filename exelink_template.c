#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "exelink_config.h"

typedef struct config_data
{
    DWORD argc;
    DWORD envc;
    wchar_t **args;
    wchar_t **envs;
} config_data;

static void free_config(config_data *cfg)
{
    DWORD i;
    if (!cfg)
        return;
    for (i = 0; i < cfg->argc; ++i)
        free(cfg->args[i]);
    for (i = 0; i < cfg->envc; ++i)
        free(cfg->envs[i]);
    free(cfg->args);
    free(cfg->envs);
}

static int read_u32(const unsigned char **p, const unsigned char *end, DWORD *out)
{
    if ((size_t)(end - *p) < 4)
        return 0;
    *out = (DWORD)(*p)[0] | ((DWORD)(*p)[1] << 8) | ((DWORD)(*p)[2] << 16) | ((DWORD)(*p)[3] << 24);
    *p += 4;
    return 1;
}

static wchar_t *read_wstr(const unsigned char **p, const unsigned char *end)
{
    DWORD len, bytes;
    wchar_t *s;
    if (!read_u32(p, end, &len))
        return NULL;
    if (len > 0x3ffffffeu)
        return NULL;
    bytes = len * 2u;
    if ((DWORD)(end - *p) < bytes)
        return NULL;
    s = (wchar_t *)calloc((size_t)len + 1u, sizeof(wchar_t));
    if (!s)
        return NULL;
    memcpy(s, *p, bytes);
    *p += bytes;
    return s;
}

static int load_config(config_data *cfg)
{
    HMODULE mod = GetModuleHandleW(NULL);
    HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(EXELINK_CONFIG_RESOURCE_ID), RT_RCDATA);
    HGLOBAL glob;
    const unsigned char *p, *end;
    DWORD size, magic, version, i;
    memset(cfg, 0, sizeof(*cfg));
    if (!res)
    {
        fwprintf(stderr, L"exelink: configuration resource not found\n");
        return 0;
    }
    glob = LoadResource(mod, res);
    size = SizeofResource(mod, res);
    p = (const unsigned char *)LockResource(glob);
    if (!glob || !p || size < 16)
    {
        fwprintf(stderr, L"exelink: invalid configuration resource\n");
        return 0;
    }
    end = p + size;
    if (!read_u32(&p, end, &magic) || !read_u32(&p, end, &version) ||
        !read_u32(&p, end, &cfg->argc) || !read_u32(&p, end, &cfg->envc) ||
        magic != EXELINK_CONFIG_MAGIC || version != EXELINK_CONFIG_VERSION)
    {
        fwprintf(stderr, L"exelink: unsupported configuration format\n");
        return 0;
    }
    cfg->args = (wchar_t **)calloc(cfg->argc ? cfg->argc : 1, sizeof(wchar_t *));
    cfg->envs = (wchar_t **)calloc(cfg->envc ? cfg->envc : 1, sizeof(wchar_t *));
    if (!cfg->args || !cfg->envs)
        return 0;
    for (i = 0; i < cfg->argc; ++i)
        if (!(cfg->args[i] = read_wstr(&p, end)))
            return 0;
    for (i = 0; i < cfg->envc; ++i)
        if (!(cfg->envs[i] = read_wstr(&p, end)))
            return 0;
    return 1;
}

static size_t quoted_len(const wchar_t *arg)
{
    size_t len = 2, bs = 0;
    const wchar_t *p;
    int quote = (*arg == L'\0');
    for (p = arg; *p; ++p)
        if (*p == L' ' || *p == L'\t' || *p == L'"')
            quote = 1;
    if (!quote)
        return wcslen(arg);
    for (p = arg; *p; ++p)
    {
        if (*p == L'\\')
        {
            ++bs;
            ++len;
        }
        else if (*p == L'"')
        {
            len += bs + 2;
            bs = 0;
        }
        else
        {
            ++len;
            bs = 0;
        }
    }
    return len + bs;
}

static void append_quoted(wchar_t **dst, const wchar_t *arg)
{
    const wchar_t *p;
    size_t bs = 0, i;
    int quote = (*arg == L'\0');
    for (p = arg; *p; ++p)
        if (*p == L' ' || *p == L'\t' || *p == L'"')
            quote = 1;
    if (!quote)
    {
        while (*arg)
            *(*dst)++ = *arg++;
        return;
    }
    *(*dst)++ = L'"';
    for (p = arg; *p; ++p)
    {
        if (*p == L'\\')
        {
            *(*dst)++ = L'\\';
            ++bs;
        }
        else if (*p == L'"')
        {
            for (i = 0; i < bs + 1; ++i)
                *(*dst)++ = L'\\';
            *(*dst)++ = L'"';
            bs = 0;
        }
        else
        {
            *(*dst)++ = *p;
            bs = 0;
        }
    }
    for (i = 0; i < bs; ++i)
        *(*dst)++ = L'\\';
    *(*dst)++ = L'"';
}

static wchar_t *make_cmdline(DWORD n, wchar_t **args)
{
    size_t len = 1, i;
    wchar_t *buf, *p;
    for (i = 0; i < n; ++i)
        len += quoted_len(args[i]) + (i ? 1 : 0);
    buf = (wchar_t *)malloc(len * sizeof(wchar_t));
    if (!buf)
        return NULL;
    p = buf;
    for (i = 0; i < n; ++i)
    {
        if (i)
            *p++ = L' ';
        append_quoted(&p, args[i]);
    }
    *p = L'\0';
    return buf;
}

int wmain(int argc, wchar_t **argv)
{
    config_data cfg;
    wchar_t **all_args, *cmdline;
    DWORD total, i, exit_code = 1;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    if (!load_config(&cfg))
        return 125;
    if (cfg.argc == 0)
    {
        fwprintf(stderr, L"exelink: no command configured\n");
        free_config(&cfg);
        return 125;
    }
    for (i = 0; i < cfg.envc; ++i)
    {
        wchar_t *eq = wcschr(cfg.envs[i], L'=');
        if (!eq || eq == cfg.envs[i])
        {
            fwprintf(stderr, L"exelink: invalid environment setting\n");
            free_config(&cfg);
            return 125;
        }
        *eq = L'\0';
        if (!SetEnvironmentVariableW(cfg.envs[i], eq + 1))
        {
            fwprintf(stderr, L"exelink: SetEnvironmentVariableW failed\n");
            free_config(&cfg);
            return 125;
        }
        *eq = L'=';
    }
    total = cfg.argc + (DWORD)(argc > 1 ? argc - 1 : 0);
    all_args = (wchar_t **)calloc(total, sizeof(wchar_t *));
    if (!all_args)
    {
        free_config(&cfg);
        return 125;
    }
    for (i = 0; i < cfg.argc; ++i)
        all_args[i] = cfg.args[i];
    for (i = cfg.argc; i < total; ++i)
        all_args[i] = argv[i - cfg.argc + 1];
    cmdline = make_cmdline(total, all_args);
    free(all_args);
    if (!cmdline)
    {
        free_config(&cfg);
        return 125;
    }
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        fwprintf(stderr, L"exelink: CreateProcessW failed: %lu\n", GetLastError());
        free(cmdline);
        free_config(&cfg);
        return 125;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    free(cmdline);
    free_config(&cfg);
    return (int)exit_code;
}
