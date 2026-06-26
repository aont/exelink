# Creating a Minimal Windows “Executable Link” Wrapper (RC Version)

This project provides a small Windows utility, **`exelink.exe`**, that behaves like a configurable “link” to another executable.  
Instead of relying on shortcuts or batch files, it stores launch settings inside the executable as an embedded resource. A companion C utility, **`restool.exe`** (built from `restool.c`), reads/writes that embedded configuration.

## What `exelink.exe` Does

`exelink.exe` is a lightweight launcher:

- It contains an embedded **argv prefix** (e.g., `cmd.exe /c`, `python.exe -E`, etc.).
- When run, it forwards any additional command-line arguments to that embedded prefix.
- It executes the target and returns the same exit code, acting as a transparent wrapper.
- Optionally, it sets environment variables before launching the child process.

This makes it useful when you want a single `.exe` that always launches a specific program without requiring extra files.

## Configuration Storage (Embedded Resource)

Configuration is stored in **one** embedded resource:

- Resource type: `RT_RCDATA`
- Resource id: `101`
- Payload: a binary “config blob” with the record format described below

### Record Format

All multi-byte integers are **uint64 little-endian**.  
All strings are **UTF-16LE bytes** with **no NUL terminator** (lengths are explicit).

Each record begins with a **type field** of exactly **8 bytes**, representing **4 UTF-16LE code units (WCHAR)**.

Type values (8 bytes each):

- `ARGV` record type: `"ARGV"` as UTF-16LE  
  Bytes: `A\0 R\0 G\0 V\0`
- `ENV` record type: `"ENV\0"` as UTF-16LE (4th WCHAR is NUL)  
  Bytes: `E\0 N\0 V\0 \0\0`
- `END` record type: `"END\0"` as UTF-16LE  
  Bytes: `E\0 N\0 D\0 \0\0`

#### ARGV record

```

{type = "ARGV" (UTF-16LE, 8 bytes)}
{argv_prefix_length: uint64 (bytes)}
{argv_prefix: argv_prefix_length bytes (UTF-16LE)}

```

- `argv_prefix` is a **Windows command-line string**, not a null-terminated string.
- `exelink.exe` uses this as the prefix of the command line it executes.

#### ENV record (repeatable)

```

{type = "ENV\0" (UTF-16LE, 8 bytes)}
{env_key_length: uint64 (bytes)}
{env_key: env_key_length bytes (UTF-16LE)}
{env_value_length: uint64 (bytes)}
{env_value: env_value_length bytes (UTF-16LE)}

```

- Environment variables are applied in record order.
- **Unset is not supported.**
- If `env_value_length == 0`, it means **set the variable to an empty string**.

#### END record

```

{type = "END\0" (UTF-16LE, 8 bytes)}

````

- Marks the end of the blob.

## `restool.exe` Interface (INI)

`restool.exe` reads/writes the config blob stored in `RT_RCDATA 101` and exposes it as an INI file. It uses the Windows profile/INI APIs so Unicode values round-trip through the same Windows APIs used to read and write INI data.

### INI Schema

Configuration is represented by two sections:

```ini
[argv]
0=C:\Windows\System32\cmd.exe
1=/c

[env]
FOO=bar
EMPTY=
```

Notes:

* `[argv]` numeric keys define the argv prefix. Keys must be contiguous from `0`.
* For compatibility, `[argv] command_line=...` is also accepted when numeric argv entries are absent.
* `[env]` maps environment variable names to values.
* To set an empty string, use `KEY=`.
* There is **no “unset”** operation.
* New or empty INI files written by `restool.exe` are created as UTF-16LE with a BOM so non-ASCII argv/env values round-trip through the Windows profile APIs.

### Commands

#### 1) Get current configuration

Output INI to stdout:

```cmd
restool get exelink.exe
```

Write INI to a file:

```cmd
restool get exelink.exe --out config.ini
```

#### 2) Set configuration

Read INI from a file:

```cmd
restool set exelink.exe --in config.ini
```

Read INI from stdin:

```cmd
type config.ini | restool set exelink.exe --in -
```

## Running Through the Wrapper

Once configured, you can run via the wrapper and pass normal arguments:

```cmd
exelink.exe echo %FOO%
```

`exelink.exe` forwards all additional arguments to the embedded `argv` prefix.

## Building `exelink.exe`

The goal is to produce a minimal executable suitable for this “link” behavior.

### MSVC Build

```text
cl /nologo /O2 /GS- /GR- /EHsc- /Zl /utf-8 exelink.c /link /NODEFAULTLIB /ENTRY:mainCRTStartup /SUBSYSTEM:CONSOLE /OUT:exelink.exe kernel32.lib user32.lib
```

### MinGW Build

```text
gcc -nodefaultlibs -nostartfiles -o exelink.exe exelink.c -lkernel32 -luser32
```

## Building `restool.exe`

`restool.exe` uses the Windows resource, profile/INI, and command-line APIs. It intentionally keeps the Python-compatible argv serialization logic in C so the embedded `ARGV` command line can round-trip through `CommandLineToArgvW`.

### MSVC Build

```text
cl /nologo /O2 /utf-8 restool.c /link /OUT:restool.exe kernel32.lib shell32.lib
```

### MinGW Build

```text
gcc -O2 -municode -o restool.exe restool.c -lkernel32 -lshell32
```

## Summary

* `exelink.exe` is a minimal Windows launcher that forwards arguments to an embedded argv prefix and applies embedded environment variables.
* Configuration is stored as a binary record stream in a **single** embedded resource (`RT_RCDATA 101`).
* `restool.exe` reads/writes that configuration using the INI schema above.
