# MinFS

MinFS is a simple, unix-like filesystem built for Magenta.

It currently supports files up to 512MB in size.

## Using MinFS

### Host Device

 * Create a disk image which stores MinFS
```shell
$ dd if=/dev/zero of=blk.bin bs=1M count=512
```
 * Execute the run magenta script on your platform with the '-d' flag
```shell
$ ./scripts/run-magenta-x86-64 -d
```

### Target Device

**WARNING**: On real hardware, "/dev/class/block/..." refers to **REAL** storage
devices (USBs, SSDs, etc).

**BE CAREFUL NOT TO FORMAT THE WRONG DEVICE.** If in doubt, only run the
following commands through QEMU.
The `lsblk` command can be used to see more information about the devices
accessible from Magenta.

 * Within Magenta, create a MinFS volume
```
> minfs /dev/class/block/000 mkfs
```
 * Within Magenta, mount the MinFS volume
```
> minfs /dev/class/block/000 mount &
```
 * On any future boots, MinFS will be mounted automatically. Any files written
   to "/data" (the MinFS mount point) will persist across boots.
