# Bootloader Setup with USB Flash Drive

These instructions prepare a USB flash drive to be the bootloader for your
device: this procedure only allows you to netboot, it won't put anything on your
internal storage. This USB flash drive can then direct your device to boot from
the freshly-built OS on your network-connected host development machine (or
alternately from the OS on the flash drive itself).

+ Format the first partition of your USB flash drive as FAT; keep it connected
+ Execute `fx set x86-64` (if you haven't already)
+ To network-boot via __GigaBoot20x6__, execute `fx mkbootloader`. This command
  does the following for you:
  + Builds Zircon (for x86, as you have set)
  + Creates a `/efi/boot` directory on your USB drive
  + Copies 'build-zircon/build-x86/bootloader/bootx64.efi' (the
    bootloader) from your host to `/EFI/BOOT/BOOTX64.EFI` on your USB drive
+ To network-boot via __zedboot__, `fx mkzedboot /path/to/your/device`. The
`mkzedboot` command does the above, as well as the following, for you:
  + Creates a zedboot.bin (from zircon.bin and bootdata.bin in your 'out' tree)
  + Creates a CMDLINE file that sets the default boot to zedboot with '0'
    timeout
  + Copies these zedboot.bin and CMDLINE files to the root of your USB drive
+ Use your host OS to safely remove the USB drive; insert it into your device
+ On your host, run `fx build` (if you haven't already), then `fx boot`
+ Connect your device to your host via built-in ethernet, then power up the
  device

Note: to enable booting from flash drive (with no network connection needed),
copy 'zircon.bin' to `/zircon.bin` on the root of flash drive. The device will
boot from that OS instead of the network. In Gigaboot, press 'm' to boot zircon
vs 'z' for zedboot, or set the default boot behavior with
[command line flags](kernel_cmdline.md).

See also:
* [Setting up the Acer device](acer12.md)
* [Setting up the NUC device](nuc.md)