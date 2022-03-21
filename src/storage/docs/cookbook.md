# Storage Cookbook

This document contains a list of tasty^W handy recipes for developing Fuchsia's storage stack.  The
intended audience is members of the Storage team, filesystem developers, et cetera.

## Attach a host file as a block device to QEMU

```shell
touch /tmp/blk.bin
truncate /tmp/blk.bin $((1024 * 1024 * 64))
fx qemu -kN -- -drive file=/tmp/blk.bin,index=0,media=disk,cache=directsync
```

The above 64M block device will then appear as a block device in Fuchsia (and can be found using
`lsblk`).

## Reformatting a block device

Note that this is destructive and can only be done on a block device which isn't currently mounted.

Note: These commands are all run from the Fuchsia shell.

```shell
# First, find the path to the block device.
lsblk
...
001  64M                                              /dev/sys/platform/pci/00:1f.2/ahci/sata0/block
...

# Format it!
mkfs /dev/sys/platform/pci/00:1f.2/ahci/sata0/block <fs_type>
```

## Mounting a filesystem

A limited version of command line filesystem mounting is supported on Fuchsia for debugging
purposes. The mount command can launch filesystems and place them in the `/mnt` directory, where
they can be accessed with that path prefix.

Note that this can only be done on a block device which isn't currently mounted, and which is
formatted appropriately.

Note: These commands are all run from the Fuchsia shell.

```shell
# First, find the block device.
lsblk
...
001  64M                                              /dev/sys/platform/pci/00:1f.2/ahci/sata0/block
...

# Mount it!
mount /dev/sys/platform/pci/00:1f.2/ahci/sata0/block /mnt/minfs

# Use it!
echo "hello world!" >> /mnt/minfs/file
mkdir /mnt/minfs/dir
cat /mnt/minfs/file
ls /mnt/minfs

# When you're done, you can unmount it.
umount /mnt/minfs
```

## Increasing the size of QEMU images

```shell
$ IMAGE_SIZE=… fx qemu …
```
