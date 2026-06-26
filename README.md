# Exelink: Creating a Minimal Windows “Executable Link” Wrapper

This project builds two Windows executables:

- **`exelink_template.exe`**: a small launcher template that reads an embedded binary configuration resource at runtime.
- **`mkexelink.exe`**: a generator that contains `exelink_template.exe` as a C byte array, writes a copy to a user-specified output path, and updates that copy's embedded configuration.

The previous INI-based configuration tool has been removed. Settings are now supplied directly as `mkexelink.exe` startup arguments.

## What the Generated Wrapper Does

A generated wrapper is a lightweight launcher:

- It contains an embedded **argv prefix** such as `cmd.exe /c` or `python.exe -E script.py`.
- When run, it appends any additional command-line arguments to that embedded prefix.
- It executes the target and returns the same exit code.
- Optionally, it sets environment variables before launching the child process.

This is useful when you want a single `.exe` that always launches a specific program without requiring shortcuts, batch files, INI files, or a separate template file at generation time.

## Generator Interface

Create a configured wrapper with:

```cmd
mkexelink OUTPUT [--env KEY=VALUE]... -- COMMAND [ARG...]
```

Arguments before `--` configure the generated wrapper:

- `OUTPUT`: where to write the generated executable.
- `--env KEY=VALUE`: environment variable to set before the generated wrapper launches its child process. Repeat as needed. `KEY=` sets an empty string.

Arguments after `--` are serialized as the generated wrapper's embedded argv prefix.

Examples:

```cmd
mkexelink hello.exe -- cmd.exe /c echo Hello
mkexelink tool.exe --env FOO=bar --env EMPTY= -- python.exe -E script.py
```

Once generated, pass additional runtime arguments directly to the generated executable:

```cmd
hello.exe from-wrapper
```

The generated wrapper appends `from-wrapper` to its embedded command line.

## Embedded Template

`mkexelink.exe` does not read `exelink_template.exe` from disk at runtime. During the build, `tools/bin2c.cpp` is compiled into a small host utility that converts `exelink_template.exe` into `exelink_template.inc`, which is included by `mkexelink.c` as a C array. At generation time, `mkexelink.exe` writes those bytes to `OUTPUT` and then updates `RT_RCDATA` resource id `101` inside that output file.

## Configuration Storage

Configuration is stored in **one** embedded resource in each generated wrapper:

- Resource type: `RT_RCDATA`
- Resource id: `101`
- Payload: a binary config blob with the record format described below

### Record Format

All multi-byte integers are **uint64 little-endian**. All strings are **UTF-16LE bytes** with **no NUL terminator** because lengths are explicit.

Each record begins with an 8-byte type field representing 4 UTF-16LE code units.

Type values:

- `ARGV`: `A\0 R\0 G\0 V\0`
- `ENV\0`: `E\0 N\0 V\0 \0\0`
- `END\0`: `E\0 N\0 D\0 \0\0`

#### ARGV record

```text
{type = "ARGV" (UTF-16LE, 8 bytes)}
{argv_prefix_length: uint64 (bytes)}
{argv_prefix: argv_prefix_length bytes (UTF-16LE)}
```

`argv_prefix` is a Windows command-line string, not a null-terminated string.

#### ENV record (repeatable)

```text
{type = "ENV\0" (UTF-16LE, 8 bytes)}
{env_key_length: uint64 (bytes)}
{env_key: env_key_length bytes (UTF-16LE)}
{env_value_length: uint64 (bytes)}
{env_value: env_value_length bytes (UTF-16LE)}
```

Environment variables are applied in record order. Unset is not supported. An empty value sets the variable to an empty string.

#### END record

```text
{type = "END\0" (UTF-16LE, 8 bytes)}
```

Marks the end of the blob.

## Building

### MSVC Build

```text
nmake /f Makefile.msvc
```

The MSVC makefile builds `exelink_template.exe`, builds the C++ `tools\bin2c.exe` converter, converts the template into `exelink_template.inc`, and then builds `mkexelink.exe`.

### MinGW Build

```text
mingw32-make -f Makefile.mingw
```

The MinGW makefile performs the same template, C++ converter, C-array, and generator build sequence.

## Summary

- `exelink_template.exe` is the original launcher executable renamed as a template.
- `mkexelink.exe` is the original resource tool renamed and redesigned as a generator.
- INI file reading/writing has been removed.
- `mkexelink.exe` embeds the template executable as C data and creates configured wrapper executables at user-specified paths.
