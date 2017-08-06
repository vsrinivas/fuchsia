# Intel NUC (Skylake and Broadwell)

WARNING:  These are directions to configure the machine and boot an experimental, in-development OS on it.

## NUC Setup & Configuration

These instructions configure the machine to boot from a USB flash drive.

1. Remove four bottom plate screws and bottom plate
2. Install memory (and optionally M.2 SSD (only SATA is supported; NVMe lacks a driver))
3. Boot into Visual BIOS (F2)
4. Select the Wrench menu (upper right), select Visual Bios Settings
5. Disable Internet Updates (Requires a mouse due to the wonders of Visual BIOS)
6. Select Advanced tab then Boot
7. Disable Legacy Boot (under the Legacy Boot Priority pane on the right)
8. Select the Boot Configuration tab in the left pane
9. Enable “Boot USB Devices First”, “Boot Network Devices Last”, and “Unlimited Boot to Network Attempts”
10. Make sure USB Legacy Support in the “Devices” section is Disabled
11. Network Boot (bottom left pane) should be showing “UEFI PXE & iSCSI”
12. F10 to save settings, Y to confirm

## GigaBoot20x6 Setup
1. Format the first partition on a USB flash drive as FAT.
2. Build Magenta for x86-64
3. The bootloader is here: build-magenta-pc-x86-64/bootloader/bootx64.efi
4. Copy `bootx64.efi` to `/EFI/BOOT/BOOTX64.EFI` on the USB flash drive.
4. Use this flash drive to network boot.
5. If you copy `magenta.bin` to `/magenta.bin` on the flash drive it will boot from the flash drive instead of the network.

## Important: network booting only works with the *built-in* ethernet port on the NUC, not via a USB dongle.
