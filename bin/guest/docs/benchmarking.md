# Hypervisor Benchmarking

This document will guide you through the process of building a Debian Linux based
system that can be used to execute benchmarks on both KVM and Zircon guests. The
approach outlined here will create a USB flash drive that is bootable on x86-64
EFI based systems.

## Setup the Flash Drive

Insert the USB drive into your host machine and run the following:
```
sudo apt install debootstrap gdisk
cd $FUCHSIA_DIR
./zircon/system/uapp/guest/scripts/build-bootable-usb-multiboot.sh
# Follow the instructions presented.
```
**Note:** If you experience apt failures during the bootstrap process check if
your firewall is blocking traffic.

This will setup an EFI system partition with GRUB configured to either boot
Zircon or Linux. The Linux image includes sufficient packages and scripts to
boot itself as a KVM guest.

## Creating a Zircon guest

Start a bootserver to serve the host kernel and bootdata. Ex:

```
$ $FUCHSIA_DIR/garnet/bin/guest/scripts/build.sh -p "garnet/packages/default" x86
```

Boot off the flash drive and select "Gigaboot" from the GRUB menu.

The appropriate block device corresponding to the USB flash drive must be
determined. We're looking for a removable device (RE flags) with 3 partitions
(EFI, Root, Home).

```
$ lsblk
ID   SIZE TYPE            LABEL                FLAGS   DEVICE
000   14G                                      RE      /dev/sys/pci/...
001  200M efi system      EFI System Partition RE      /dev/sys/pci/...
002    5G unknown         Root                 RE      /dev/sys/pci/...
003    2G unknown         Home                 RE      /dev/sys/pci/...

```
In the example above, the correct device is `000`. Using that block device we can
create a guest:

```
$ mkdir /system/data/guest
# This should be the path for the "EFI System Partition" above.
$ mount -r /dev/class/block/001 /system/data/guest
# The initrd and kernel names will change as the kernel is updated. Use whatever
# is on your system.
$ launch guest -b /dev/class/block/000 -c "root=/dev/vda2 ro lockfs" -r /system/data/guest/initrd.img-4.14.0-2-amd64 /sytem/data/guest/vmlinuz-4.14.0-2-amd64
```
Upon a successful boot you should see a login prompt with default login
credentials provided.

## Creating a KVM guest

Boot off the flash drive and select "Debian" from the GRUB menu and login with
the credentials displayed on the login screen. Use the included script to create
a QEMU/KVM guest:

```
sudo /opt/run-qemu.sh
```

Once booted login to the guest using the credentials on the login screen.

## Running benchmarks

First we need to mount the RW home partition that holds the benchmarks and we
can write our results to. The device name is slightly different on KVM and
Zircon guests:

```
# Zircon Guests:
sudo mount -o rw /dev/vda3 /home

# QEMU/KVM Guests:
sudo mount -o rw /dev/vdb /home
```

### Run unixbench

```
cd /home/bench/byte-unixbench-master/UnixBench
./Run
```
