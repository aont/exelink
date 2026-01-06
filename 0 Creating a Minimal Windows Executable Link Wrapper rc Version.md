# Creating a Minimal Windows “Executable Link” Wrapper (rc Version)

This project provides a small Windows utility, **`exelink.exe`**, that behaves like a configurable “link” to another executable. Instead of relying on shortcuts or batch files, it stores a launch target (and optional settings) inside the executable as embedded resources. A companion Python script, **`restool.py`**, writes and reads that embedded configuration.

## What `exelink.exe` Does

`exelink.exe` is a lightweight launcher:

* It contains an embedded **command prefix** (for example, the path to `cmd.exe` or `python.exe`).
* When run, it forwards any additional command-line arguments to the embedded target.
* It executes the target and returns the same exit code, acting as a transparent wrapper.

This makes it useful when you want a single `.exe` that always launches a specific program without requiring extra files.

## How to Use `restool.py`

`restool.py` manages the embedded configuration in `exelink.exe`. The README focuses on the `cmd` subcommand, which reads/writes the command prefix stored in the executable.

### 1) Read the Current Target

If you run the `cmd` subcommand without providing a new target, it prints the currently embedded executable path:

```cmd
> python restool.py cmd exelink.exe
C:\Windows\System32\cmd.exe
```

### 2) Write a New Target

To embed a new command prefix, pass the desired executable path (and optionally arguments) after the executable name:

```cmd
> python restool.py cmd exelink.exe 'C:\Program Files\Python312\python.exe'
```

### 3) Verify the Updated Target

Reading again shows the updated embedded target:

```cmd
> python restool.py cmd exelink.exe
C:\Program Files\Python312\python.exe
```

### 4) Run Through the Wrapper

After embedding a Python executable, you can invoke Python via the wrapper and pass normal arguments:

```cmd
> ./exelink.exe -c "import sys; print(sys.version)"
3.12.9 (tags/v3.12.9:fdb8142, Feb  4 2025, 15:27:58) [MSC v.1942 64 bit (AMD64)]
```

In other words, `exelink.exe` becomes a portable entry point to your chosen Python installation.

## Building `exelink.exe`

The README includes build commands for both MSVC and MinGW. The goal is to produce a minimal executable suitable for this “link” behavior.

### MSVC Build

```text
cl /nologo /O2 /GS- /GR- /EHsc- /Zl /utf-8 exelink.c /link /NODEFAULTLIB /ENTRY:mainCRTStartup /SUBSYSTEM:CONSOLE /OUT:exelink.exe kernel32.lib user32.lib
```

### MinGW Build

```text
gcc -nodefaultlibs -nostartfiles -o exelink.exe exelink.c -lkernel32 -luser32
```

These commands compile `exelink.c` into `exelink.exe` and link only the required Windows libraries.

## Summary

* **`exelink.exe`** is a minimal Windows launcher that forwards arguments to an embedded target executable.
* **`restool.py`** writes and reads the embedded command prefix so you can retarget the wrapper (e.g., from `cmd.exe` to a specific `python.exe`).
* The provided MSVC and MinGW commands build the wrapper as a small standalone executable.

This makes `exelink.exe` a practical way to distribute a consistent command entry point while keeping the actual target configurable.
