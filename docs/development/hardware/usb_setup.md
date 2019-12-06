# Prepare a USB flash drive to be a bootable disk

These instructions prepare a USB flash drive to be a bootable disk for your
device: this procedure only enables you to netboot or pave, it won't put
anything on your internal storage. This USB flash drive can then direct your
device to boot from the freshly-built OS on your network-connected host
development machine (or alternately from the OS on the flash drive itself).

+ Execute `fx set core.x64` (if you haven't already)
+ Create a __zedboot__ key using, `fx mkzedboot /path/to/your/device`. The
`mkzedboot` command does the following:
  + Creates a FAT partition continaing an EFI System Partition, containing
    the Gigaboot EFI bootloader and a configuration that specifies to always
    boot into Zedboot.
  + Creates a ChromeOS bootable partition with a developer key signed Zedboot
    kernel partition.
+ On your host, run `fx build` (if you haven't already).
+ If you wish to install Fuchsia to the target device (modifying the target
  device harddisk), run `fx pave` on the host. IF you only wish to "netboot"
  the target device, and avoid modifying any disk state, run `fx netboot` on
  the host instead.
+ Connect your device to your host via built-in ethernet, then power up the
  device.

## Manual Configuration

It is also relatively easy to manually create an EFI boot key with particular
properites, though this will only boot on EFI systems.

+ Format the USB key with a blank FAT partition.
+ Create a directory called `EFI/BOOT`.
+ Copy `bootx64.efi` from `build-x64/bootloader` of a Zircon build into the
  above directory.
+ Copy `zircon.bin` from `build-x64` of a Zircon build into the root
  directory of the FAT partition.
+ Copy `zedboot.bin` from `build-x64` of a Zircon build into the root
  directory of the FAT partition.
+ Optionally: Create a file called `cmdline` in the root fo the FAT
  partition. This file may contain any directives documented in
  [command line flags](/docs/reference/kernel/kernel_cmdline.md).

The created disk will by default boot from zircon.bin instead of the network.
At the Gigaboot screen, press 'm' to boot zircon vs 'z' for zedboot, or set
the default boot behavior with the `bootloader.default` flag in `cmdline`.

See also:

* [Setting up the Acer device](acer12.md)
* [Setting up the NUC device](/docs/development/hardware/intel_nuc.md)
* [Command line flags](/docs/reference/kernel/kernel_cmdline.md)
