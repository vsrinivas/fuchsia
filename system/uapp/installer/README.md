# Fuchsia Installer

## Motivation

The goal of the Fuchsia installer is to provide a stop-gap solution to get as
much of the system operating from internal storage of a device as possible. The
installer will eventually be superseded by a robust update/refresh mechanism.
Running Fuchsia from internal storage will make booting faster and reduce memory
pressure because the secondary bootfs will no longer be stored in RAM. This also
allows you to experience more of the real performance of the system and exercise
the storage stack.

To accomplish this the installer will put an
[EFI system partition](https://en.wikipedia.org/wiki/EFI_system_partition), a
Fuchsia system partition, and a Fuchsia data partition on the same drive. Ideally
this will be an internal disk on whatever device you are using to run Fuchsia,
but the installer will happily install Fuchsia on removable media or in fact
anything that looks like an appealing block device.

## Limitations

The installer is substantially limited in flexibility as it is considered a bridge
solution until a more robust, shipping solution for install and refresh can be
developed. There will be improvements made to the installer before it is
retired, but the investment budget for this component is limited.

The development host parts of this process have only been tried on Linux. In
theory the only thing that actually requires Linux is the script that creates
the bootable gigaboot USB drive.

## Installed partitions and target device requirements

The installer requires at least 5GiB of free space. This is a fairly arbitrary
amount and is based on best guess estimates of the minimum size we needed to make
various partitions comfortable for the near future. It is also simple to tune the
space requirements.

The installer adds an EFI system partition ("ESP"). The ESP will contain the
Zircon kernel and the gigaboot bootloader. The partition size is set at 1GiB,
which is far more than the components need, but this size is chosen to accomodate
any files for booting other operating systems off the same ESP. This partition is
FAT32 formatted.

The next partition the installer is concerned with is the Fuchsia system
partition which contains what we would think of as Fuchsia. This partition size
is set at 4GiB. Again, this is several times larger than is actually needed, but
is selected to account for substantial growth while balancing against time needed
to write it. This partition is formatted with MinFS.

The installer tries to add a data partition meant for arbitrary data storage by
applications. The installer will attempt to make this partition 8GB, but will
settle for making it as small as 200MiB. If it can not find at least 200MiB it
will not add a data partition. If added, this partition is formatted with MinFS.

## Preparing device and media for install

The installer is not very intelligent about where it installs things. It simply
looks for the first suitable place it can find and plunks down the blocks. It
**does not** know or care if the thing it finds is internal storage or removable
media. For this reason it is important to understand the primitive way the
installer decides where to write data and we describe below. This will allow
you to configure your storage device(s) in such a way that Fuchsia installs
where you want.

The installer uses the following procedure after which it writes data to the
partitions it identified as the targets.

1. Does disk contain a 4GiB partition with the Fuchsia system partition GUID and
a 1GiB [ESP](https://en.wikipedia.org/wiki/EFI_system_partition) that is not the
first disk partition?
2. Does the disk contain 5GiB of free space? If yes, go to (4)
3. Ask the user for a partition to delete, go to step 2
4. Create ESP and Fuchsia system partitions

The installer avoids updating the first disk partition if it is an ESP partition
because this will tend to be where ESP data from a commercial device might reside
and this data might not be restorable.

If you're installing with media like a USB drive inserted, an easy way to make
the USB drive unattractive to Fushia is not to have a Fuchsia system partition
on the USB drive and to have all the space on the USB drive allocated to
one or more partitions.

## Preparing the install files

Get a copy of liblz4-tool and mtools, probably these are available via a package
system like apt, brew, or similar.

```
> sudo apt-get install liblz4-tool mtools
```

[Build Fuchsia](https://fuchsia.googlesource.com/docs/+/master/getting_started.md#Build-Fuchsia)
as you normally would, then run the command to build the installer files

```
> scripts/installer/build-installable-userfs.sh
```

By default this script assumes you're doing a debug build for x86-64. Use the
'-h' option to get help customizing the parameters for other architectures, if
you're doing a release build, or if your source and output directory structure
is unique. If you had a user.bootfs in your output directory, it will be moved
to user-noinstaller.bootfs.

If you will boot from a USB drive, use the 'build-bootable-usb-gigaboot.sh'
script to configure a USB drive

```
> scripts/build-bootable-usb-gigaboot.sh
> sync
```

## Running the installer
Boot your device with netboot or with the USB drive you created above. The
installer's first preference is to update existing ESP and Fuchsia system
partitions as described in the previous section. Failing this it will look for
available space where it can create those partitions. If neither of these is
possible the installer will ask the user to delete disk partitions to make space
available. Unfortunately the installer can not resize partitions in a way that
will preserve the data on them, if you need this, please repartition the disk
with some other tool.

The installer requires that its target drive contains a GPT. Your drive may not
have a GPT if this is the first time you've used it or you've done a low-level
format of the drive. To add a GPT locate your drive with 'lsblk'

```
> lsblk
ID     SIZE      TYPE          LABEL       FLAGS  DEVICE
000      28G                               RE     /dev/pci/00:14:00/xhci/usb/004/ifc-000/ums/lun-000/block
001    1023M     efi system    EFI SYSTEM  RE     /dev/pci/00:14:00/xhci/usb/004/ifc-000/ums/lun-000/block/part-000/block
002      27G     data          FAT PARTITION RE   /dev/pci/00:14:00/xhci/usb/004/ifc-000/ums/lun-000/block/part-001/block
003     232G                                      /dev/pci/00:17:00/ahci/sata2/block
```

In this case device 003 is what we want based on it not being labeled removable
and its size being the same size as the internal storage drive. To add a GPT to
it we can use the GPT tool and then reboot.

```
> gpt init /dev/class/block/003
> dm reboot
```

Start the installer

```
> install-fuchsia
```

If eligible partitions are found or can be created, the installer will display
how much data has been written. Note that the installer takes an all-or-nothing
approach to detection and installation. If it finds a system partition, but no
ESP, it will try to add a second system partition and an ESP. As such, if you
remove one, remove both.

If the installer needs space it will print out the current disk and partition
configuration. If you had two disks attached it might look something like

```
Disk 0 (/dev/class/block/000) 6.0GB
       Partition 0              EFI 1.0GB at block 8388642
Disk 1 (/dev/class/block/001) 24.0GB
       Partition 0              6th 3.9GB at block 41943074
       Partition 1           system 4.0GB at block 25166458
       Partition 2              EFI 1.0GB at block 33555066
       Partition 3             data 8.0GB at block 34

```

The installer will then ask you what it can delete.

```
Delete a partition on which disk (0-1 blank to cancel)?
```

If I had selected disk 1 it would then ask which partition

```
Which partition would you like to remove? (0-3)
```

If deleting that partition makes enough space available, the installer will then
proceed. If more space is needed, it will ask the same questions again.

If at least 200MiB of disk space is available and a Fuchsia data partition is
not already on the disk, one will be created. If a Fuchsia data partition does
already exist, nothing will be done.

Installation should now be complete and you can power off your device, remove
any storage or networking cables you no longer desire and power back on.

```
> dm poweroff
```

## Additional notes
### Dual/multi-booting
If you want to run Fuchsia on a dual-boot device, additional effort may be
required. The ESP we use is configured such that it will always take precendence
over other ESPs you may have on the device. To deal with this, you may use the
'gpt' tool to hide our ESP from the UEFI bootloader. All you need to hide the
ESP is a bootable zircon. To unhide the ESP you'll need a different way to boot
zircon, such as a USB drive, or another tool that can manipulate the GPT.

First use 'lsblk' to examine the block devices you have

```
> lsblk
ID  SIZE TYPE           LABEL    FLAGS  DEVICE
000   6G                                /dev/pci/00:17:00/ahci/sata1/block
001  24G                                /dev/pci/00:17:00/ahci/sata2/block
002   4G unknown        minfs1          /dev/pci/00:17:00/ahci/sata1/block/part-000/block
003   1G unknown        extra           /dev/pci/00:17:00/ahci/sata1/block/part-001/block
004    3G unknown        6th            /dev/pci/00:17:00/ahci/sata2/block/part-000/block
005    4G unknown        system         /dev/pci/00:17:00/ahci/sata2/block/part-001/block
006    1G efi system     EFI            /dev/pci/00:17:00/ahci/sata2/block/part-002/block
007    8G unknown        data           /dev/pci/00:17:00/ahci/sata2/block/part-003/block
```

The two 'ahci'-driven entries here are actually disks, the rest are partitions.
Let's look at device 001.

```
> gpt dump /dev/class/block/001
blocksize=512 blocks=50331648
Partition table is valid
0: 6th 0x2800022 0x2ffffdd (7fffbc blocks)
    id:   28172C34-6660-E3E6-70E8-7BFB6D3B4100
    type: FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF
1: system 0x180027a 0x2000279 (800000 blocks)
    id:   FD7021F4-B61C-7B7F-2DAB-A41F6E40C926
    type: 506B000B-B7C7-4653-A7D5-B737332C889D
2: EFI 0x200027a 0x2200279 (200000 blocks)
    id:   A14D8A47-2F6E-FFAC-DFB6-E9E6D06786B2
    type: C12A7328-F81F-11D2-BA4B-00A0C93EC93B
3: data 0x22 0x1000021 (1000000 blocks)
    id:   B1E4A0A2-0AED-B7FC-2304-F052CE7AD781
    type: 08185F0C-892D-428A-A789-DBEEC8F55E6A
Total: 4 partitions
```

Based on the type of partition 2 I can see this really seems like an ESP. If I
want the UEFI bootloader to ignore it, I can hide it. Using the 'gpt visible'
command, which takes a partition index, and a device path in addition to whether
or not the partition should be hidden or visible.

```
> gpt visible 2 false /dev/class/block/001
WARNING: You are about to permanently alter /dev/class/block/001

Type 'y' to continue, any other key to cancel
y
blocksize=512 blocks=50331648
GPT changes complete.
```

To make the partition visible again, you'll need to boot zircon from an
external drive or use another tool that can manipulate the GPT of the device. If
you can boot zircon, just change the above 'false' to 'true'.
