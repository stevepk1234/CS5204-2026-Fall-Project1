# CS5204 - File System Checker

## What it does

xcheck checks the .img file and returns an error if it detects any consistencies according to the specification

## How it works

Block sanitation is entirely done through pointer arithmetic using mmap(). A pointer is placed at the start of the superblock and each block is
referenced by jumping BSIZE * block_num bytes ahead of the superblock. The architecture is specified in xv6-riscv/fs.h.

## How it is used

Run 
```bash
$ make
```

Then run 
```bash
$ ./xcheck <file_system_image>
```
