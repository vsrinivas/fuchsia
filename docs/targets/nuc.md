# Intel NUC (Skylake and Broadwell)

WARNING:  These are directions to configure a NUC machine to load and boot an
experimental, in-development OS.


## NUC Setup & Configuration

These instructions configure the NUC machine to boot from a USB flash drive.
This is a necessary step for _network boot_, where the bootloader on your USB
drive pulls your freshly-built OS across the network, from host machine to NUC.

1. Install memory (and optional SSD)
  + Remove four bottom plate screws and bottom plate
  + Install memory in the DIMM slot(s)
  + (Optional) Install SSD in M.2 slot (SATA support only; NVMe lacks a driver)
2. Boot the machine into Visual BIOS
  + Reinstall the bottom plate, attach power, and start the machine
  + Press F2 during startup to enter Visual BIOS setup
  + Mouse will be required, due to the wonders of Visual BIOS
3. Disable BIOS updates from internet (setting may not be present in newer NUCs)
  + Select the Wrench menu (upper right), then Visual Bios Settings
  + Deselect __Internet Updates__
4. Verify that your memory (and SSD) are correctly installed and detected
  + Select Advanced settings, then Main section
  + Right-side Memory Information pane shoudl list your memory
  + Switch to Devices section
  + Select PCI tab, verify that __M.2 Slot__ is enabled
  + Select SATA tab, verify that __Chipset SATA__ is enabled
  + Both tabs (PCI and SATA) should show your SSD
5. Disable USB legacy and legacy boot
  + Still in Devices section, select USB tab
  + Deselect __USB Legacy__ support
  + In Boot section, select Priority tab
  + Deselect __Legacy Boot__ (in right-side Legacy Boot Priority pane)
6. Configure boot ordering
  + Select Boot Configuration tab
  + Enable __Boot USB Devices First__, __Boot Network Devices Last__, and
  __Unlimited Boot to Network Attempts__
  + Network Boot (bottom left pane) should display _UEFI PXE & iSCSI_
7. Save BIOS changes
  + Press F10 (or click the top right (x) button) to Save and Exit, Y to confirm
  + Device will automatically reboot and begin looking for a USB or network boot
8. Power down the NUC


## Bootloader Setup with USB Flash Drive

These instructions prepare a USB flash drive to be the bootloader for your NUC
device. This USB flash drive can then direct your NUC to boot from the
freshly-built OS on your network-connected host development machine (or
alternately from the OS on the flash drive itself).

+ Format the first partition of your USB flash drive as FAT; keep it connected
+ Execute `fx set x86-64` (if you haven't already)
+ To network-boot via __GigaBoot20x6__, execute `fx mkbootloader`. This command
  does the following for you:
  + Builds Zircon (for x86-64, as you have set)
  + Creates a `/efi/boot` directory on your USB drive
  + Copies 'build-zircon/build-zircon-pc-x86-64/bootloader/bootx64.efi' (the
    bootloader) from your host to `/EFI/BOOT/BOOTX64.EFI` on your USB drive
+ To network-boot via __zedboot__, `fx mkzedboot`. The 'mkzedboot' command does
  the above, as well as the following, for you:
  + Creates a zedboot.bin (from zircon.bin and bootdata.bin in your 'out' tree)
  + Creates a CMDLINE file that sets the default boot to zedboot with '0'timeout
  + Copies these zedboot.bin and CMDLINE files to the root of your USB drive
+ Use your host OS to safely remove the USB drive; insert it into your NUC
+ On your host, run `fx build` (if you haven't already), then `fx boot`
+ Connect your NUC to your host via built-in ethernet, then power up the NUC

Note: to boot from flash drive (no network connection needed), copy 'zircon.bin'
to `/zircon.bin` on the root of flash drive. The NUC will boot from that OS
instead of the network.

### Important: network booting only works with the NUC's *built-in* ethernet
### Net-boot via USB-ethernet dongle is unsupported
