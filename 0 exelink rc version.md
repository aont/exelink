# exelink rc version

```bash
$ gcc -nostartfiles -o exelink.exe exelink.c

$ python restool.py write exelink.exe 'C:\Program Files\Python312\python.exe'

$ python restool.py read exelink.exe
C:\Program Files\Python312\python.exe

$ ./exelink.exe -c "import sys; print(sys.version)"
3.12.9 (tags/v3.12.9:fdb8142, Feb  4 2025, 15:27:58) [MSC v.1942 64 bit (AMD64)]
```
