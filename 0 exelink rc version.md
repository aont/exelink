# exelink rc version

```bash
windres resource.rc -O coff -o resource.res
gcc -nostartfiles -o exelink.exe exelink.c resource.res
```