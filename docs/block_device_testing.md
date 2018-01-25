# Block device testing

__WARNING: All of the following tests are destructive, and they may not
ask for confirmation before executing. Run at your own risk.__

## Protocol testing

*blktest* is an integration which may be used to check adherence to the block protocol.

```shell
$ blktest -d /dev/class/block/000
```

## Filesystem testing

*fs-test* is a filesystem integration test suite that can be used to verify
Fuchsia filesystem correctness on a filesystem.

To avoid racing with the auto-mounter, it is recommended to run this
test with the kernel command line option "zircon.system.disable-automount=true".

TODO(ZX-1604): Ensure this filesystem test suite can execute on large
partitions. It is currently recommended to use this test on a 1-2 GB GPT
partition on the block device.

```shell
$ /boot/test/fs/fs-test -d /dev/class/block/000 -f minfs
```

## Correctness testing

*iochk* is a tool which pseudorandomly reads and writes to a block device to check for errors.

```shell
$ iochk -bs 32k -t 8 /dev/class/block/000
```

## Performance testing

*iotime* is a benchmarking tool which tests the read and write performance of block devices.

```shell
$ iotime read fifo /dev/class/block/000 64m 4k
```


