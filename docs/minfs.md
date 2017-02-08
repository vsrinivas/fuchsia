# MinFS

MinFS is a simple, unix-like filesystem built for Magenta.

It currently supports files up to 512MB in size.

## Using MinFS

### Host Device

 * Create a disk image which stores MinFS
```shell
$ dd if=/dev/zero of=blk.bin bs=1M count=512
```
 * Execute the run magenta script on your platform with the '--' to pass
   arguments directly to Qemu and then use '-hda' to point to the file. If you
   wish to attach additional devices, you can supply them with '-hdb', '-hdc,
   and so on.
```shell
$ ./scripts/run-magenta-x86-64 -- -hda blk.bin
```

### Target Device

**WARNING**: On real hardware, "/dev/class/block/..." refers to **REAL** storage
devices (USBs, SSDs, etc).

**BE CAREFUL NOT TO FORMAT THE WRONG DEVICE.** If in doubt, only run the
following commands through QEMU.
The `lsblk` command can be used to see more information about the devices
accessible from Magenta.

 * Within magenta, add a GPT to the raw device. Note that the third argument
   passed is the number of blocks on the device, less 68 blocks for GPT data.
   17K (34, 512-byte blocks) is required at the beginning and end of the disk
   to hold GPT and MBR data. The numbers here assume 512-byte blocks and should
   be adjusted accordingly if your block size is different.
```
> gpt add 34 1048508 myvol /dev/class/block/000
```

 * Run 'lsblk' again, you should see output similar to the below. Note that 000
   and 002 actually refer to the same physical device. A duplicate has been
   created. This seems wrong and will probably be fixed in the future.
```
$ lsblk
ID  DEV      DRV      SIZE TYPE           LABEL
.
..
000 sata0    ahci     512M
002 sata0 (aligned) align    512M
003 sata0 (aligned)p0 gpt      511M unknown        myvol
```

 * Within Magenta, format the partition as MinFS. Using 'lsblk' you should see
   a block device which is the whole disk and a slightly smaller device which
   is the partition. In the above output the partition is device 003 and would
   have the path '/dev/class/block/003'
```
> mkfs <PARTITION_PATH> minfs
```

 * If you want the device to be mounted automatically on reboot, use the GPT
   tool to set its type. As we did above, **you must** use 'lsblk' **again**
   to locate the entry for the disk. In the same output above the device path
   is /dev/class/block/002. We want to edit the type of the zero-th partition.
   Here we use the keyword 'DATA' to set the type GUID, but if you wanted to
   use an arbitrary GUID you would supply it where 'DATA' is used.
```
> gpt edit 0 type DATA <DEVICE_PATH>
```

 * On any future boots, the partition will be mounted automatically at /data.
 
 * If you don't want the partition to be mounted automatically, you can simply
   mount it manually.
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
