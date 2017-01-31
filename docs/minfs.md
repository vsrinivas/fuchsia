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
 * Within Magenta, format the partition as MinFS. Using 'lsblk' you should see
   a block device which is the whole disk and a slightly smaller device which
   is the partition.
```
> mkfs <PARTITION_PATH> minfs
```
 * Within Magenta, mount the MinFS volume
```
> mount <PARTITION_PATH> /data
```
 * If you want the device to be mounted automatically on reboot, use the GPT
   tool to set it's type. **You must** use 'lsblk' **again** to locate the
   entry for the disk. As a result of the GPT changes a duplicate entry for
   the block device has probably been created. You might see two entries for
   the disk that look identical, use the device entry with the highest index,
   for example 002 vs 000.
 ```
 > gpt edit 0 type 08185f0c-892d-428a-a789-dbeec8f55e6a <DEVICE_PATH>
 ```
 * On any future boots, MinFS will be mounted automatically at /data.
 * Any files written to "/data" (the mount point for this GUID) will persist
   across boots. To test this, try making a file on the new MinFS volume,
   rebooting, and observing it still exists.
```
> touch /data/foobar
> dm reboot
> ls /data
```
