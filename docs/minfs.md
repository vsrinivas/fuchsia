# MinFS

MinFS is a simple, unix-like filesystem built for Magenta.

It currently supports files up to 512MB in size.

## Using MinFS

### Host Device (QEMU Only)

 * Create a disk image which stores MinFS
```shell
(Linux)
$ truncate --size=16G blk.bin
(Mac)
$ mkfile -n 16g blk.bin
```
 * Execute the run magenta script on your platform with the '--' to pass
   arguments directly to QEMU and then use '-hda' to point to the file. If you
   wish to attach additional devices, you can supply them with '-hdb', '-hdc,
   and so on.
```shell
$ ./scripts/run-magenta-x86-64 -- -hda blk.bin
```

### Target Device (QEMU and Real Hardware)

**WARNING**: On real hardware, "/dev/class/block/..." refers to **REAL** storage
devices (USBs, SSDs, etc).

**BE CAREFUL NOT TO FORMAT THE WRONG DEVICE.** If in doubt, only run the
following commands through QEMU.
The `lsblk` command can be used to see more information about the devices
accessible from Magenta.

 * Within magenta, 'lsblk' can be used to list the block devices currently on
   the system. On this example system below, "/dev/class/block/000" is a raw
   block device.
```
> lsblk
ID  DEV      DRV      SIZE TYPE           LABEL
000 block    block     16G
```
 * Let's add a GPT to this block device.
```
> gpt init /dev/class/block/000
...
> lsblk
ID  DEV      DRV      SIZE TYPE           LABEL
002 block    block     16G
```
 * Now that we have a GPT on this device, let's check what we can do with it.
   (NOTE: after manipulating the gpt, the device number may change. Use lsblk
   to keep track of how to refer to the block device).
```
> gpt dump /dev/class/block/002
blocksize=512 blocks=33554432
Partition table is valid
GPT contains usable blocks from 34 to 33554398 (inclusive)
Total: 0 partitions
```
 * "gpt dump" tells us some important info: it tells us (1) How big blocks are,
   and (2) which blocks we can actually use.
   Let's fill part of the disk with a MinFS filesystem.
```
> gpt add 34 20000000 minfs /dev/class/block/002
```
 * Within Magenta, format the partition as MinFS. Using 'lsblk' you should see
   a block device which is the whole disk and a slightly smaller device which
   is the partition. In the above output, the partition is device 003, and would
   have the path '/dev/class/block/003'
```
> mkfs <PARTITION_PATH> minfs
```

 * If you want the device to be mounted automatically on reboot, use the GPT
   tool to set its type. As we did above, **you must** use 'lsblk' **again**
   to locate the entry for the disk. We want to edit the type of the zero-th
   partition.  Here we use the keyword 'DATA' to set the type GUID, but if you
   wanted to use an arbitrary GUID you would supply it where 'DATA' is used.
```
> gpt edit 0 type DATA <DEVICE_PATH>
```

 * On any future boots, the partition will be mounted automatically at /data.

 * If you don't want the partition to be mounted automatically, you can update
   the visibility (or GUID) of the partition, and simply mount it manually.
```
> mount <PARTITION_PATH> /data
```

 * Any files written to "/data" (the mount point for this GUID) will persist
   across boots. To test this, try making a file on the new MinFS volume,
   rebooting, and observing it still exists.
```
> touch /data/foobar
> dm reboot
> ls /data
```

 * To find out which block device/file system is mounted at each subdirectory
   under a given path, use the following command:
```
> lsfs -b <PATH>
```