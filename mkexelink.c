#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "exelink_config.h"

extern const unsigned char exelink_template_exe[];
extern const size_t exelink_template_exe_size;

static void usage(void)
{
    fprintf(stderr, "usage: mkexelink -o output.exe [-e NAME=VALUE]... -- command [prefix-arg ...]\n");
}

static int append_bytes(unsigned char **buf, size_t *len, size_t *cap, const void *src, size_t n)
{
    if (*len + n < *len)
        return 0;
    if (*len + n > *cap)
    {
        size_t nc = *cap ? *cap * 2 : 256;
        unsigned char *nb;
        while (nc < *len + n)
            nc *= 2;
        nb = (unsigned char *)realloc(*buf, nc);
        if (!nb)
            return 0;
        *buf = nb;
        *cap = nc;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    return 1;
}
static int append_u32(unsigned char **b, size_t *l, size_t *c, DWORD v)
{
    unsigned char x[4];
    x[0] = (unsigned char)v;
    x[1] = (unsigned char)(v >> 8);
    x[2] = (unsigned char)(v >> 16);
    x[3] = (unsigned char)(v >> 24);
    return append_bytes(b, l, c, x, 4);
}

static char *wide_to_mb(const wchar_t *s)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    char *m;
    if (!n)
        return NULL;
    m = (char *)malloc((size_t)n);
    if (!m)
        return NULL;
    if (!WideCharToMultiByte(CP_UTF8, 0, s, -1, m, n, NULL, NULL))
    {
        free(m);
        return NULL;
    }
    return m;
}

static void free_mb_argv(char **argv, int argc)
{
    int i;
    if (!argv)
        return;
    for (i = 0; i < argc; ++i)
        free(argv[i]);
    free(argv);
}

static char **wide_argv_to_mb_argv(int argc, wchar_t **wargv)
{
    char **argv = (char **)calloc((size_t)argc + 1, sizeof(char *));
    int i;
    if (!argv)
        return NULL;
    for (i = 0; i < argc; ++i)
    {
        argv[i] = wide_to_mb(wargv[i]);
        if (!argv[i])
        {
            free_mb_argv(argv, argc);
            return NULL;
        }
    }
    return argv;
}

static wchar_t *mb_to_wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    wchar_t *w;
    if (!n)
        n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    if (!n)
        return NULL;
    w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!w)
        return NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n) &&
        !MultiByteToWideChar(CP_ACP, 0, s, -1, w, n))
    {
        free(w);
        return NULL;
    }
    return w;
}
static int append_wstr(unsigned char **b, size_t *l, size_t *c, const char *s)
{
    wchar_t *w = mb_to_wide(s);
    DWORD n;
    int ok;
    if (!w)
        return 0;
    n = (DWORD)wcslen(w);
    ok = append_u32(b, l, c, n) && append_bytes(b, l, c, w, (size_t)n * sizeof(wchar_t));
    free(w);
    return ok;
}

int wmain(int argc, wchar_t **wargv)
{
    const char *out = NULL;
    char **argv = NULL;
    char **envs = NULL;
    int envc = 0, envcap = 0, opt, i;
    unsigned char *cfg = NULL;
    size_t cfg_len = 0, cfg_cap = 0;
    FILE *f;
    HANDLE h;
    BOOL ok;
    wchar_t *wout;

    argv = wide_argv_to_mb_argv(argc, wargv);
    if (!argv)
    {
        fprintf(stderr, "mkexelink: failed to convert command line arguments to UTF-8\n");
        return 1;
    }
    optind = 1;

    while ((opt = getopt(argc, argv, "o:e:h")) != -1)
    {
        switch (opt)
        {
        case 'o':
            out = optarg;
            break;
        case 'e':
            if (envc == envcap)
            {
                int nc = envcap ? envcap * 2 : 8;
                char **ne = (char **)realloc(envs, (size_t)nc * sizeof(char *));
                if (!ne)
                {
                    free(envs);
                    free_mb_argv(argv, argc);
                    return 1;
                }
                envs = ne;
                envcap = nc;
            }
            envs[envc++] = optarg;
            break;
        case 'h':
            usage();
            free(envs);
            free_mb_argv(argv, argc);
            return 0;
        default:
            usage();
            free(envs);
            free_mb_argv(argv, argc);
            return 2;
        }
    }
    if (!out || optind >= argc)
    {
        usage();
        free(envs);
        free_mb_argv(argv, argc);
        return 2;
    }
    wout = mb_to_wide(out);
    if (!wout)
    {
        free(envs);
        free_mb_argv(argv, argc);
        return 1;
    }
    f = _wfopen(wout, L"wb");
    if (!f)
    {
        perror(out);
        free(wout);
        free(envs);
        free_mb_argv(argv, argc);
        return 1;
    }
    if (fwrite(exelink_template_exe, 1, exelink_template_exe_size, f) != exelink_template_exe_size)
    {
        perror(out);
        fclose(f);
        free(wout);
        free(envs);
        free_mb_argv(argv, argc);
        return 1;
    }
    fclose(f);

    if (!append_u32(&cfg, &cfg_len, &cfg_cap, EXELINK_CONFIG_MAGIC) || !append_u32(&cfg, &cfg_len, &cfg_cap, EXELINK_CONFIG_VERSION) ||
        !append_u32(&cfg, &cfg_len, &cfg_cap, (DWORD)(argc - optind)) || !append_u32(&cfg, &cfg_len, &cfg_cap, (DWORD)envc))
    {
        free(wout);
        free(envs);
        free(cfg);
        free_mb_argv(argv, argc);
        return 1;
    }
    for (i = optind; i < argc; ++i)
        if (!append_wstr(&cfg, &cfg_len, &cfg_cap, argv[i]))
        {
            free(wout);
            free(envs);
            free(cfg);
            free_mb_argv(argv, argc);
            return 1;
        }
    for (i = 0; i < envc; ++i)
    {
        if (!strchr(envs[i], '=') || envs[i][0] == '=')
        {
            fprintf(stderr, "mkexelink: invalid environment setting: %s\n", envs[i]);
            free(wout);
            free(envs);
            free(cfg);
            free_mb_argv(argv, argc);
            return 2;
        }
        if (!append_wstr(&cfg, &cfg_len, &cfg_cap, envs[i]))
        {
            free(wout);
            free(envs);
            free(cfg);
            free_mb_argv(argv, argc);
            return 1;
        }
    }
    h = BeginUpdateResourceW(wout, FALSE);
    if (!h)
    {
        fprintf(stderr, "mkexelink: BeginUpdateResourceW failed: %lu\n", GetLastError());
        free(wout);
        free(envs);
        free(cfg);
        free_mb_argv(argv, argc);
        return 1;
    }
    ok = UpdateResourceW(h, RT_RCDATA, MAKEINTRESOURCEW(EXELINK_CONFIG_RESOURCE_ID), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), cfg, (DWORD)cfg_len);
    if (!EndUpdateResourceW(h, !ok) || !ok)
    {
        fprintf(stderr, "mkexelink: resource update failed: %lu\n", GetLastError());
        free(wout);
        free(envs);
        free(cfg);
        free_mb_argv(argv, argc);
        return 1;
    }
    free(wout);
    free(cfg);
    free(envs);
    free_mb_argv(argv, argc);
    return 0;
}
