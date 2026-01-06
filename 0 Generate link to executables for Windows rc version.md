# Generate link to executables for Windows (rc version)

## usage

```cmd
> python restool.py create exelink.exe 'C:\Windows\System32\cmd.exe'

> python restool.py read exelink.exe
C:\Windows\System32\cmd.exe

> python restool.py write exelink.exe 'C:\Program Files\Python312\python.exe'

> python restool.py read exelink.exe
C:\Program Files\Python312\python.exe

> ./exelink.exe -c "import sys; print(sys.version)"
3.12.9 (tags/v3.12.9:fdb8142, Feb  4 2025, 15:27:58) [MSC v.1942 64 bit (AMD64)]
```

## to generate exelink.exe

- msvc: `cl /nologo /O2 /GS- /GR- /EHsc- /Zl /utf-8 exelink.c /link /NODEFAULTLIB /ENTRY:mainCRTStartup /SUBSYSTEM:CONSOLE /OUT:exelink.exe kernel32.lib user32.lib
`
- mingw: `gcc -nodefaultlibs -nostartfiles -o exelink.exe exelink.c -lkernel32 -luser32`