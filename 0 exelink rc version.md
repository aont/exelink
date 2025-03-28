# exelink rc version

```bash
$ windres resource.rc -O coff -o resource.res
$ gcc -nostartfiles -o exelink.exe exelink.c resource.res

$ python restool.py read exelink.exe
cmd.exe

$ python restool.py write exelink.exe 'C:\Program Files\Python312\python.exe'
```
