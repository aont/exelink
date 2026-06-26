# exelink

`exelink` builds small Windows wrapper executables. A generated wrapper stores a fixed command prefix and environment variable settings in its resources. At runtime it appends the wrapper's own arguments, applies the configured environment variables, launches the resulting command with `CreateProcessW`, waits for it, and returns the child exit code.

## Build

### MSYS2 UCRT64 / MinGW-w64

```sh
mingw32-make -f Makefile.mingw
```

### MSVC nmake

Build `getopt.lib` from [`getopt-msvc-helper`](https://github.com/aont/getopt-msvc-helper) first, then place or reference it with `GETOPT_DIR`.

```bat
nmake /f Makefile.msvc GETOPT_DIR=path\to\getopt-msvc-helper
```

## Usage

```sh
mkexelink -o wrapper.exe [-e NAME=VALUE]... -- command [fixed-arg ...]
```

Example:

```sh
mkexelink -o python_utf8.exe -e PYTHONUTF8=1 -- C:\Windows\py.exe -3
python_utf8.exe script.py
```

The wrapper runs `C:\Windows\py.exe -3 script.py` with `PYTHONUTF8=1` set.
