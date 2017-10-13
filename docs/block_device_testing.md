# Block device testing

## Performance testing

*iotime* is a benchmarking tool which tests the read and write performance of block devices.

```shell
$ iotime read fifo /dev/class/block/000 64m 4k
```

## Correctness testing

*iochk* is a tool which pseudorandomly reads and writes to a block device to check for errors.

```shell
$ iochk -bs 32k -t 8 /dev/class/block/001
```
