#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>

#define CONFIG_RESOURCE_ID 101
#define MAX_SECTION_CHARS (1024 * 1024)

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
static ULONGLONG read_u64(const BYTE *p)
{
    ULONGLONG v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}
static int type_eq(const BYTE *p, const BYTE *t) { return memcmp(p, t, 8) == 0; }

static int append_wchar(BYTE_BUF *b, WCHAR c) { return append_bytes(b, &c, sizeof(c)); }
static int append_wstr(BYTE_BUF *b, LPCWSTR s) { return append_bytes(b, s, wcslen(s) * sizeof(WCHAR)); }
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
static BYTE *read_resource(LPCWSTR exe, DWORD *size)
{
    HMODULE m = LoadLibraryExW(exe, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!m)
    {
        die_last(L"LoadLibraryExW");
        return NULL;
    }
    HRSRC r = FindResourceW(m, MAKEINTRESOURCEW(CONFIG_RESOURCE_ID), MAKEINTRESOURCEW(RT_RCDATA));
    if (!r)
    {
        FreeLibrary(m);
        fwprintf(stderr, L"Config (RT_RCDATA 101) not found.\n");
        return NULL;
    }
    DWORD n = SizeofResource(m, r);
    HGLOBAL hg = LoadResource(m, r);
    void *p = hg ? LockResource(hg) : NULL;
    BYTE *copy = NULL;
    if (n && p)
    {
        copy = (BYTE *)malloc(n);
        if (copy)
        {
            memcpy(copy, p, n);
            *size = n;
        }
    }
    FreeLibrary(m);
    return copy;
}
static int parse_blob(BYTE *data, DWORD size, ARGV_LIST *argv, ENV_MAP *env)
{
    BYTE *p = data, *end = data + size;
    LPWSTR cmd = NULL;
    while (end - p >= 8)
    {
        BYTE *t = p;
        p += 8;
        if (type_eq(t, TYPE_END))
            break;
        if (end - p < 8)
            goto fail;
        ULONGLONG len = read_u64(p);
        p += 8;
        if (len > (ULONGLONG)(end - p) || len % 2)
            goto fail;
        if (type_eq(t, TYPE_ARGV))
        {
            cmd = (LPWSTR)malloc((SIZE_T)len + sizeof(WCHAR));
            if (!cmd)
                goto fail;
            memcpy(cmd, p, (SIZE_T)len);
            cmd[len / 2] = 0;
            p += (SIZE_T)len;
            continue;
        }
        if (type_eq(t, TYPE_ENV))
        {
            LPWSTR k = (LPWSTR)malloc((SIZE_T)len + sizeof(WCHAR));
            if (!k)
                goto fail;
            memcpy(k, p, (SIZE_T)len);
            k[len / 2] = 0;
            p += (SIZE_T)len;
            if (!*k)
            {
                free(k);
                goto fail;
            }
            if (end - p < 8)
            {
                free(k);
                goto fail;
            }
            ULONGLONG vlen = read_u64(p);
            p += 8;
            if (vlen > (ULONGLONG)(end - p) || vlen % 2)
            {
                free(k);
                goto fail;
            }
            LPWSTR v = (LPWSTR)malloc((SIZE_T)vlen + sizeof(WCHAR));
            if (!v)
            {
                free(k);
                goto fail;
            }
            memcpy(v, p, (SIZE_T)vlen);
            v[vlen / 2] = 0;
            p += (SIZE_T)vlen;
            ENV_PAIR *ni = (ENV_PAIR *)realloc(env->items, (env->count + 1) * sizeof(ENV_PAIR));
            if (!ni)
            {
                free(k);
                free(v);
                goto fail;
            }
            env->items = ni;
            env->items[env->count].key = k;
            env->items[env->count].value = v;
            env->count++;
            continue;
        }
        goto fail;
    }
    if (!cmd)
        goto fail;
    int argc = 0;
    LPWSTR *av = CommandLineToArgvW(cmd, &argc);
    free(cmd);
    if (!av)
        goto fail;
    argv->argv = (LPWSTR *)calloc(argc, sizeof(LPWSTR));
    if (!argv->argv)
    {
        LocalFree(av);
        goto fail;
    }
    argv->argc = argc;
    for (int i = 0; i < argc; i++)
    {
        argv->argv[i] = _wcsdup(av[i]);
        if (!argv->argv[i])
        {
            LocalFree(av);
            goto fail;
        }
    }
    LocalFree(av);
    return 1;
fail:
    free(cmd);
    fwprintf(stderr, L"invalid config blob\n");
    return 0;
}
static int ensure_ini(LPCWSTR path)
{
    DWORD a = GetFileAttributesW(path);
    if (a == INVALID_FILE_ATTRIBUTES)
    {
        HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE)
            return 0;
        WORD bom = 0xfeff;
        DWORD wr;
        WriteFile(h, &bom, 2, &wr, NULL);
        CloseHandle(h);
    }
    return 1;
}
static int write_ini(LPCWSTR path, ARGV_LIST *argv, ENV_MAP *env)
{
    ensure_ini(path);
    WritePrivateProfileStringW(L"argv", NULL, NULL, path);
    WritePrivateProfileStringW(L"env", NULL, NULL, path);
    wchar_t key[32];
    for (int i = 0; i < argv->argc; i++)
    {
        swprintf(key, 32, L"%d", i);
        if (!WritePrivateProfileStringW(L"argv", key, argv->argv[i], path))
            return 0;
    }
    for (DWORD i = 0; i < env->count; i++)
        if (!WritePrivateProfileStringW(L"env", env->items[i].key, env->items[i].value, path))
            return 0;
    WritePrivateProfileStringW(NULL, NULL, NULL, path);
    return 1;
}
static LPWSTR get_section(LPCWSTR sec, LPCWSTR path)
{
    DWORD sz = 4096;
    for (;;)
    {
        LPWSTR b = (LPWSTR)calloc(sz, sizeof(WCHAR));
        DWORD n = GetPrivateProfileSectionW(sec, b, sz, path);
        if (n < sz - 2)
            return b;
        free(b);
        sz *= 2;
        if (sz > MAX_SECTION_CHARS)
            return NULL;
    }
}
static int add_env(ENV_MAP *e, LPCWSTR k, LPCWSTR v)
{
    ENV_PAIR *ni = (ENV_PAIR *)realloc(e->items, (e->count + 1) * sizeof(ENV_PAIR));
    if (!ni)
        return 0;
    e->items = ni;
    e->items[e->count].key = _wcsdup(k);
    e->items[e->count].value = _wcsdup(v ? v : L"");
    if (!e->items[e->count].key || !e->items[e->count].value)
        return 0;
    e->count++;
    return 1;
}
static int read_ini(LPCWSTR path, ARGV_LIST *argv, ENV_MAP *env)
{
    LPWSTR s = get_section(L"argv", path);
    if (!s)
        return 0;
    int max = -1, count = 0;
    for (LPWSTR p = s; *p; p += wcslen(p) + 1)
    {
        wchar_t *eq = wcschr(p, L'=');
        if (!eq)
            continue;
        *eq = 0;
        wchar_t *endp;
        long idx = wcstol(p, &endp, 10);
        if (*p && !*endp && idx >= 0)
        {
            if (idx > max)
                max = (int)idx;
            count++;
        }
        *eq = L'=';
    }
    if (count)
    {
        if (max + 1 != count)
        {
            fwprintf(stderr, L"[argv] numeric keys must be contiguous from 0\n");
            free(s);
            return 0;
        }
        argv->argc = count;
        argv->argv = (LPWSTR *)calloc(count, sizeof(LPWSTR));
        for (LPWSTR p = s; *p; p += wcslen(p) + 1)
        {
            wchar_t *eq = wcschr(p, L'=');
            if (!eq)
                continue;
            *eq = 0;
            wchar_t *endp;
            long idx = wcstol(p, &endp, 10);
            if (*p && !*endp && idx >= 0)
                argv->argv[idx] = _wcsdup(eq + 1);
        }
    }
    else
    {
        wchar_t cmd[32768];
        GetPrivateProfileStringW(L"argv", L"command_line", L"", cmd, 32768, path);
        if (!*cmd)
        {
            fwprintf(stderr, L"INI must contain [argv] numeric entries or [argv] command_line\n");
            free(s);
            return 0;
        }
        LPWSTR *av = CommandLineToArgvW(cmd, &argv->argc);
        argv->argv = (LPWSTR *)calloc(argv->argc, sizeof(LPWSTR));
        for (int i = 0; i < argv->argc; i++)
            argv->argv[i] = _wcsdup(av[i]);
        LocalFree(av);
    }
    free(s);
    s = get_section(L"env", path);
    if (!s)
        return 0;
    for (LPWSTR p = s; *p; p += wcslen(p) + 1)
    {
        wchar_t *eq = wcschr(p, L'=');
        if (eq)
        {
            *eq = 0;
            if (*p && !add_env(env, p, eq + 1))
            {
                free(s);
                return 0;
            }
        }
    }
    free(s);
    return 1;
}
static int dump_stdout(ARGV_LIST *a, ENV_MAP *e)
{
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wcscat(tmp, L"restool_out.ini");
    if (!write_ini(tmp, a, e))
        return 0;
    HANDLE h = CreateFileW(tmp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    DWORD sz = GetFileSize(h, NULL);
    BYTE *buf = (BYTE *)malloc(sz);
    DWORD rd;
    ReadFile(h, buf, sz, &rd, NULL);
    CloseHandle(h);
    DeleteFileW(tmp);
    if (sz >= 2 && buf[0] == 0xff && buf[1] == 0xfe)
    {
        int w = (sz - 2) / 2;
        int n = WideCharToMultiByte(CP_UTF8, 0, (WCHAR *)(buf + 2), w, NULL, 0, NULL, NULL);
        char *u = (char *)malloc(n + 2);
        WideCharToMultiByte(CP_UTF8, 0, (WCHAR *)(buf + 2), w, u, n, NULL, NULL);
        fwrite(u, 1, n, stdout);
        if (n == 0 || u[n - 1] != '\n')
            fputc('\n', stdout);
        free(u);
    }
    else
        fwrite(buf, 1, sz, stdout);
    free(buf);
    return 1;
}
static int read_stdin_to_temp(LPWSTR path)
{
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    GetTempFileNameW(dir, L"rti", 0, path);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    char buf[4096];
    size_t n;
    DWORD wr;
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0)
        WriteFile(h, buf, (DWORD)n, &wr, NULL);
    CloseHandle(h);
    return 1;
}
static void usage(void) { fwprintf(stderr, L"usage:\n  restool get EXE [--out PATH|-]\n  restool set EXE [--in PATH|-]\n"); }
int wmain(int argc, wchar_t **wargv)
{
    if (argc < 3)
    {
        usage();
        return 2;
    }
    int isget = wcscmp(wargv[1], L"get") == 0, isset = wcscmp(wargv[1], L"set") == 0;
    if (!isget && !isset)
    {
        usage();
        return 2;
    }
    LPCWSTR exe = wargv[2];
    LPCWSTR path = L"-";
    for (int i = 3; i < argc; i++)
    {
        if (isget && !wcscmp(wargv[i], L"--out") && i + 1 < argc)
            path = wargv[++i];
        else if (isset && !wcscmp(wargv[i], L"--in") && i + 1 < argc)
            path = wargv[++i];
        else
        {
            usage();
            return 2;
        }
    }
    ARGV_LIST a = {0};
    ENV_MAP e = {0};
    int ok = 0;
    if (isget)
    {
        DWORD sz;
        BYTE *d = read_resource(exe, &sz);
        if (d && parse_blob(d, sz, &a, &e))
            ok = (!wcscmp(path, L"-") || !*path) ? dump_stdout(&a, &e) : write_ini(path, &a, &e);
        free(d);
    }
    else
    {
        wchar_t tmp[MAX_PATH];
        LPCWSTR in = path;
        int del = 0;
        if (!wcscmp(path, L"-") || !*path)
        {
            if (!read_stdin_to_temp(tmp))
                return 1;
            in = tmp;
            del = 1;
        }
        if (read_ini(in, &a, &e))
        {
            BYTE_BUF b = {0};
            if (build_blob(&a, &e, &b))
                ok = write_resource(exe, b.data, (DWORD)b.len);
            free(b.data);
        }
        if (del)
            DeleteFileW(tmp);
    }
    if (!ok)
        die_last(L"restool");
    free_config(&a, &e);
    return ok ? 0 : 1;
}
