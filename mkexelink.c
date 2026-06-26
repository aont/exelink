#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

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

#pragma function(memcpy)
#pragma function(memset)
void *__cdecl memcpy(void *dst, const void *src, size_t n)
{
    BYTE *d = (BYTE *)dst;
    const BYTE *s = (const BYTE *)src;
    while (n--)
        *d++ = *s++;
    return dst;
}
void *__cdecl memset(void *dst, int c, size_t n)
{
    BYTE *d = (BYTE *)dst;
    BYTE v = (BYTE)c;
    while (n--)
        *d++ = v;
    return dst;
}

static HANDLE process_heap(void) { return GetProcessHeap(); }
static void *xalloc(SIZE_T bytes) { return HeapAlloc(process_heap(), 0, bytes ? bytes : 1); }
static void *xzalloc(SIZE_T bytes) { return HeapAlloc(process_heap(), HEAP_ZERO_MEMORY, bytes ? bytes : 1); }
static void *xrealloc(void *p, SIZE_T bytes) { return p ? HeapReAlloc(process_heap(), 0, p, bytes ? bytes : 1) : xalloc(bytes); }
static void xfree(void *p)
{
    if (p)
        HeapFree(process_heap(), 0, p);
}

static SIZE_T xwcslen(LPCWSTR s)
{
    SIZE_T n = 0;
    while (s[n])
        n++;
    return n;
}
static int xwcscmp(LPCWSTR a, LPCWSTR b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return (int)(*a - *b);
}
static LPCWSTR xwcschr(LPCWSTR s, WCHAR c)
{
    while (*s)
    {
        if (*s == c)
            return s;
        s++;
    }
    return c == 0 ? s : NULL;
}
static LPWSTR xwcsdup(LPCWSTR s)
{
    SIZE_T bytes = (xwcslen(s) + 1) * sizeof(WCHAR);
    LPWSTR r = (LPWSTR)xalloc(bytes);
    if (r)
        memcpy(r, s, bytes);
    return r;
}

static void write_stderr(LPCWSTR s)
{
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD written = 0;
    if (h != INVALID_HANDLE_VALUE && h != NULL)
        WriteFile(h, s, (DWORD)(xwcslen(s) * sizeof(WCHAR)), &written, NULL);
}
static void append_dec(WCHAR *buf, DWORD *pos, DWORD value)
{
    WCHAR tmp[10];
    DWORD n = 0;
    do
    {
        tmp[n++] = (WCHAR)(L'0' + (value % 10));
        value /= 10;
    } while (value);
    while (n)
        buf[(*pos)++] = tmp[--n];
}
static void die_last(LPCWSTR msg)
{
    WCHAR buf[256];
    DWORD pos = 0;
    for (SIZE_T i = 0; msg[i] && pos < 180; i++)
        buf[pos++] = msg[i];
    static const WCHAR suffix[] = L" failed. Error=";
    for (SIZE_T i = 0; suffix[i]; i++)
        buf[pos++] = suffix[i];
    append_dec(buf, &pos, GetLastError());
    buf[pos++] = L'\r';
    buf[pos++] = L'\n';
    buf[pos] = 0;
    write_stderr(buf);
}
static void free_config(ARGV_LIST *a, ENV_MAP *e)
{
    if (a && a->argv)
    {
        for (int i = 0; i < a->argc; i++)
            xfree(a->argv[i]);
        xfree(a->argv);
        a->argv = NULL;
        a->argc = 0;
    }
    if (e && e->items)
    {
        for (DWORD i = 0; i < e->count; i++)
        {
            xfree(e->items[i].key);
            xfree(e->items[i].value);
        }
        xfree(e->items);
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
        {
            if (nc > ((SIZE_T)-1) / 2)
            {
                nc = need;
                break;
            }
            nc *= 2;
        }
        BYTE *nd = (BYTE *)xrealloc(b->data, nc);
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
    xfree(b.data);
    return NULL;
}
static int build_blob(ARGV_LIST *argv, ENV_MAP *env, BYTE_BUF *out)
{
    LPWSTR cmd = argv_to_command_line(argv->argc, argv->argv);
    if (!cmd)
        return 0;
    SIZE_T cmd_bytes = xwcslen(cmd) * sizeof(WCHAR);
    int ok = append_bytes(out, TYPE_ARGV, 8) && append_u64(out, cmd_bytes) && append_bytes(out, cmd, cmd_bytes);
    xfree(cmd);
    if (!ok)
        return 0;
    for (DWORD i = 0; i < env->count; i++)
    {
        SIZE_T kb = xwcslen(env->items[i].key) * sizeof(WCHAR), vb = xwcslen(env->items[i].value) * sizeof(WCHAR);
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
    LPCWSTR eq = xwcschr(spec, L'=');
    if (!eq || eq == spec)
    {
        write_stderr(L"environment setting must be KEY=VALUE\r\n");
        return 0;
    }
    SIZE_T key_chars = (SIZE_T)(eq - spec);
    LPWSTR key = (LPWSTR)xzalloc((key_chars + 1) * sizeof(WCHAR));
    if (!key)
        return 0;
    memcpy(key, spec, key_chars * sizeof(WCHAR));
    ENV_PAIR *ni = (ENV_PAIR *)xrealloc(e->items, (e->count + 1) * sizeof(ENV_PAIR));
    if (!ni)
    {
        xfree(key);
        return 0;
    }
    e->items = ni;
    e->items[e->count].key = key;
    e->items[e->count].value = xwcsdup(eq + 1);
    if (!e->items[e->count].value)
    {
        xfree(key);
        e->items[e->count].key = NULL;
        return 0;
    }
    e->count++;
    return 1;
}
static void usage(void)
{
    write_stderr(L"usage:\r\n"
                 L"  mkexelink OUTPUT [--env KEY=VALUE]... -- COMMAND [ARG...]\r\n\r\n"
                 L"examples:\r\n"
                 L"  mkexelink hello.exe -- cmd.exe /c echo Hello\r\n"
                 L"  mkexelink tool.exe --env FOO=bar -- python.exe -E script.py\r\n");
}
static int wmain_nocrt(int argc, wchar_t **wargv)
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
        if (xwcscmp(wargv[i], L"--") == 0)
        {
            command_index = i + 1;
            break;
        }
        if (xwcscmp(wargv[i], L"--env") == 0 && i + 1 < argc)
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
    argv_prefix.argv = (LPWSTR *)xzalloc((SIZE_T)argv_prefix.argc * sizeof(LPWSTR));
    if (!argv_prefix.argv)
    {
        free_config(NULL, &env);
        return 1;
    }
    for (int i = 0; i < argv_prefix.argc; i++)
    {
        argv_prefix.argv[i] = xwcsdup(wargv[command_index + i]);
        if (!argv_prefix.argv[i])
        {
            free_config(&argv_prefix, &env);
            return 1;
        }
    }
    BYTE_BUF blob = {0};
    int ok = build_blob(&argv_prefix, &env, &blob) && write_file(output, EXELINK_TEMPLATE_ARRAY, (DWORD)EXELINK_TEMPLATE_ARRAY_LEN) && write_resource(output, blob.data, (DWORD)blob.len);
    xfree(blob.data);
    free_config(&argv_prefix, &env);
    if (!ok)
    {
        DeleteFileW(output);
        return 1;
    }
    return 0;
}
void __cdecl mainCRTStartup(void)
{
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int exit_code = argv ? wmain_nocrt(argc, argv) : 1;
    if (argv)
        LocalFree(argv);
    ExitProcess((UINT)exit_code);
}
