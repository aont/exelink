#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>

#define CONFIG_RESOURCE_ID 101
static const BYTE TYPE_ARGV[8] = {'A', 0, 'R', 0, 'G', 0, 'V', 0};
static const BYTE TYPE_ENV[8] = {'E', 0, 'N', 0, 'V', 0, 0, 0};
static const BYTE TYPE_END[8] = {'E', 0, 'N', 0, 'D', 0, 0, 0};

typedef struct
{
    int argc;
    LPWSTR *argv;
} ARGV_LIST;
typedef struct
{
    LPWSTR key;
    LPWSTR value;
} ENV_PAIR;
typedef struct
{
    DWORD count;
    ENV_PAIR *items;
} ENV_MAP;
typedef struct
{
    BYTE *data;
    SIZE_T len;
    SIZE_T cap;
} BYTE_BUF;

static void die_last(const wchar_t *msg) { fwprintf(stderr, L"%ls failed. Error=%lu\n", msg, GetLastError()); }
static void free_config(ARGV_LIST *a, ENV_MAP *e)
{
    if (a && a->argv)
    {
        for (int i = 0; i < a->argc; i++)
            free(a->argv[i]);
        free(a->argv);
        a->argv = NULL;
        a->argc = 0;
    }
    if (e && e->items)
    {
        for (DWORD i = 0; i < e->count; i++)
        {
            free(e->items[i].key);
            free(e->items[i].value);
        }
        free(e->items);
        e->items = NULL;
        e->count = 0;
    }
}
static int append_bytes(BYTE_BUF *b, const void *p, SIZE_T n)
{
    if (n > ((SIZE_T)-1) - b->len)
        return 0;
    SIZE_T need = b->len + n;
    if (need > b->cap)
    {
        SIZE_T nc = b->cap ? b->cap * 2 : 256;
        while (nc < need)
            nc *= 2;
        BYTE *nd = (BYTE *)realloc(b->data, nc);
        if (!nd)
            return 0;
        b->data = nd;
        b->cap = nc;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
    return 1;
}
static int append_u64(BYTE_BUF *b, ULONGLONG v)
{
    BYTE x[8];
    for (int i = 0; i < 8; i++)
        x[i] = (BYTE)(v >> (i * 8));
    return append_bytes(b, x, 8);
}
static int append_wchar(BYTE_BUF *b, WCHAR c) { return append_bytes(b, &c, sizeof(c)); }
static int needs_quote(LPCWSTR s)
{
    if (!*s)
        return 1;
    for (; *s; s++)
        if (*s == L' ' || *s == L'\t')
            return 1;
    return 0;
}
static LPWSTR argv_to_command_line(int argc, LPWSTR *argv)
{
    BYTE_BUF b = {0};
    for (int i = 0; i < argc; i++)
    {
        if (i && !append_wchar(&b, L' '))
            goto fail;
        LPCWSTR arg = argv[i];
        int quote = needs_quote(arg);
        SIZE_T bs = 0;
        if (quote && !append_wchar(&b, L'"'))
            goto fail;
        for (SIZE_T j = 0; arg[j]; j++)
        {
            WCHAR c = arg[j];
            if (c == L'\\')
            {
                bs++;
                continue;
            }
            if (c == L'"')
            {
                for (SIZE_T k = 0; k < bs * 2 + 1; k++)
                    if (!append_wchar(&b, L'\\'))
                        goto fail;
                bs = 0;
                if (!append_wchar(&b, L'"'))
                    goto fail;
                continue;
            }
            while (bs)
            {
                if (!append_wchar(&b, L'\\'))
                    goto fail;
                bs--;
            }
            if (!append_wchar(&b, c))
                goto fail;
        }
        if (quote)
        {
            while (bs)
            {
                if (!append_wchar(&b, L'\\') || !append_wchar(&b, L'\\'))
                    goto fail;
                bs--;
            }
            if (!append_wchar(&b, L'"'))
                goto fail;
        }
        else
            while (bs)
            {
                if (!append_wchar(&b, L'\\'))
                    goto fail;
                bs--;
            }
    }
    if (!append_wchar(&b, L'\0'))
        goto fail;
    return (LPWSTR)b.data;
fail:
    free(b.data);
    return NULL;
}

static int build_blob(ARGV_LIST *argv, ENV_MAP *env, BYTE_BUF *out)
{
    LPWSTR cmd = argv_to_command_line(argv->argc, argv->argv);
    if (!cmd)
        return 0;
    SIZE_T cmd_bytes = wcslen(cmd) * sizeof(WCHAR);
    int ok = append_bytes(out, TYPE_ARGV, 8) && append_u64(out, cmd_bytes) && append_bytes(out, cmd, cmd_bytes);
    free(cmd);
    if (!ok)
        return 0;
    for (DWORD i = 0; i < env->count; i++)
    {
        SIZE_T kb = wcslen(env->items[i].key) * sizeof(WCHAR), vb = wcslen(env->items[i].value) * sizeof(WCHAR);
        if (!*env->items[i].key)
            return 0;
        if (!(append_bytes(out, TYPE_ENV, 8) && append_u64(out, kb) && append_bytes(out, env->items[i].key, kb) && append_u64(out, vb) && append_bytes(out, env->items[i].value, vb)))
            return 0;
    }
    return append_bytes(out, TYPE_END, 8);
}
static int write_resource(LPCWSTR exe, BYTE *data, DWORD len)
{
    HANDLE h = BeginUpdateResourceW(exe, FALSE);
    if (!h)
    {
        die_last(L"BeginUpdateResourceW");
        return 0;
    }
    if (!UpdateResourceW(h, MAKEINTRESOURCEW(RT_RCDATA), MAKEINTRESOURCEW(CONFIG_RESOURCE_ID), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), data, len))
    {
        die_last(L"UpdateResourceW");
        EndUpdateResourceW(h, TRUE);
        return 0;
    }
    if (!EndUpdateResourceW(h, FALSE))
    {
        die_last(L"EndUpdateResourceW");
        return 0;
    }
    return 1;
}


#ifndef EXELINK_TEMPLATE_ARRAY
#include "exelink_template.inc"
#define EXELINK_TEMPLATE_ARRAY exelink_template_exe
#define EXELINK_TEMPLATE_ARRAY_LEN exelink_template_exe_len
#endif

static int write_file(LPCWSTR path, const BYTE *data, DWORD len)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        die_last(L"CreateFileW");
        return 0;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(h, data, len, &written, NULL) && written == len;
    CloseHandle(h);
    if (!ok)
    {
        die_last(L"WriteFile");
        return 0;
    }
    return 1;
}

static int add_env(ENV_MAP *e, LPCWSTR spec)
{
    LPCWSTR eq = wcschr(spec, L'=');
    if (!eq || eq == spec)
    {
        fwprintf(stderr, L"environment setting must be KEY=VALUE: %ls\n", spec);
        return 0;
    }
    SIZE_T key_chars = (SIZE_T)(eq - spec);
    LPWSTR key = (LPWSTR)calloc(key_chars + 1, sizeof(WCHAR));
    if (!key)
        return 0;
    memcpy(key, spec, key_chars * sizeof(WCHAR));

    ENV_PAIR *ni = (ENV_PAIR *)realloc(e->items, (e->count + 1) * sizeof(ENV_PAIR));
    if (!ni)
    {
        free(key);
        return 0;
    }
    e->items = ni;
    e->items[e->count].key = key;
    e->items[e->count].value = _wcsdup(eq + 1);
    if (!e->items[e->count].value)
        return 0;
    e->count++;
    return 1;
}

static void usage(void)
{
    fwprintf(stderr,
             L"usage:\n"
             L"  mkexelink OUTPUT [--env KEY=VALUE]... -- COMMAND [ARG...]\n\n"
             L"examples:\n"
             L"  mkexelink hello.exe -- cmd.exe /c echo Hello\n"
             L"  mkexelink tool.exe --env FOO=bar -- python.exe -E script.py\n");
}

int wmain(int argc, wchar_t **wargv)
{
    if (argc < 4)
    {
        usage();
        return 2;
    }

    LPCWSTR output = wargv[1];
    ENV_MAP env = {0};
    int command_index = -1;
    for (int i = 2; i < argc; i++)
    {
        if (wcscmp(wargv[i], L"--") == 0)
        {
            command_index = i + 1;
            break;
        }
        if (wcscmp(wargv[i], L"--env") == 0 && i + 1 < argc)
        {
            if (!add_env(&env, wargv[++i]))
            {
                free_config(NULL, &env);
                return 1;
            }
            continue;
        }
        usage();
        free_config(NULL, &env);
        return 2;
    }

    if (command_index < 0 || command_index >= argc)
    {
        usage();
        free_config(NULL, &env);
        return 2;
    }

    ARGV_LIST argv_prefix = {0};
    argv_prefix.argc = argc - command_index;
    argv_prefix.argv = (LPWSTR *)calloc(argv_prefix.argc, sizeof(LPWSTR));
    if (!argv_prefix.argv)
    {
        free_config(NULL, &env);
        return 1;
    }
    for (int i = 0; i < argv_prefix.argc; i++)
    {
        argv_prefix.argv[i] = _wcsdup(wargv[command_index + i]);
        if (!argv_prefix.argv[i])
        {
            free_config(&argv_prefix, &env);
            return 1;
        }
    }

    BYTE_BUF blob = {0};
    int ok = build_blob(&argv_prefix, &env, &blob) &&
             write_file(output, EXELINK_TEMPLATE_ARRAY, (DWORD)EXELINK_TEMPLATE_ARRAY_LEN) &&
             write_resource(output, blob.data, (DWORD)blob.len);

    free(blob.data);
    free_config(&argv_prefix, &env);
    if (!ok)
    {
        DeleteFileW(output);
        return 1;
    }
    return 0;
}
