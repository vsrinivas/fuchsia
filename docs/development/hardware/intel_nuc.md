# Install Fuchsia on a NUC

This guide provides instructions on how to get a
NUC (Next Unit of Computing) device up and running with Fuchsia.

The steps are:

1. [Get parts](#get-parts).
1. [Prepare the NUC](#prepare-the-nuc).
1. [Enable EFI booting](#enable-efi-booting).
1. [Build Fuchsia](#build-fuchsia).
1. [Prepare a bootstrap USB drive](#prepare-usb).
1. [Pave Fuchsia](#pave-fuchsia).

## 1. Get parts {#get-parts}

To get started, you need the following parts:

*  USB 3.0 Drive
*  NUC
*  RAM
*  m.2 SSD
*  Keyboard
*  Monitor that supports HDMI
*  HDMI cable
*  Ethernet cable
*  Phillips-head screwdriver with a magnetic tip

For GPU support, get a NUC7 (Kaby Lake) or NUC8 (Coffee Lake). The list below shows some example models:

 * [Intel® NUC Kit NUC7i5DNKE](https://ark.intel.com/content/www/us/en/ark/products/122486/intel-nuc-kit-nuc7i5dnke.html)
 * [Intel® NUC Kit NUC7i5DNHE](https://ark.intel.com/content/www/us/en/ark/products/122488/intel-nuc-kit-nuc7i5dnhe.html)
 * [Intel® NUC Kit NUC7i3DNKE](https://ark.intel.com/content/www/us/en/ark/products/122495/intel-nuc-kit-nuc7i3dnke.html)
 * [Intel® NUC Kit NUC7i3DNHE](https://ark.intel.com/content/www/us/en/ark/products/122498/intel-nuc-kit-nuc7i3dnhe.html)
 * [Intel® NUC Kit NUC8i5BEK](https://ark.intel.com/content/www/us/en/ark/products/126147/intel-nuc-kit-nuc8i5bek.html)
 * [Intel® NUC Kit NUC8i5BEH](https://ark.intel.com/content/www/us/en/ark/products/126148/intel-nuc-kit-nuc8i5beh.html)
 * [Intel® NUC Kit NUC8i3BEK](https://ark.intel.com/content/www/us/en/ark/products/126149/intel-nuc-kit-nuc8i3bek.html)
 * [Intel® NUC Kit NUC8i3BEH](https://ark.intel.com/content/www/us/en/ark/products/126150/intel-nuc-kit-nuc8i3beh.html)

The table below shows some example parts with links to the manufacturer's product pages:

| Item | Link | Notes |
| ---- | ---- | ------ |
| RAM | [Crucial 8GB DDR4-2400 SODIMM](https://www.crucial.com/memory/ddr4/ct8g4sfs824a) | Works fine. |
| SSD | [Samsung SSD 850 EVO SATA M.2 250GB](https://www.samsung.com/us/computing/memory-storage/solid-state-drives/ssd-850-evo-m-2-250gb-mz-n5e250bw/) | Works fine. |
| SSD | [ADATA Ultimate SU800 M.2 2280 3D NAND SSD](https://www.adata.com/upload/downloadfile/Datasheet_SU800%20M.2%202280_EN_202003.pdf) | Works fine. |
| SSD | [CRUCIAL MX300 SSD](https://www.crucial.com/products/ssd/mx300-ssd) | Works fine, but is discontinued. |
| Monitor | [HP 22cwa](https://support.hp.com/au-en/document/c04837546) | Works fine. |

## 2. Prepare the NUC {#prepare-the-nuc}

NUCs do not come with RAM or an SSD, so you need to manually install them.

<img width="40%" src="/docs/images/developing_on_nuc/parts.jpg"/>

**Figure 1**. A NUC device and RAM and SSD sticks.

To install the RAM and SSD on your NUC, do the following:

1. Remove the Phillips screws on the bottom feet of the NUC.

   <img width="40%" src="/docs/images/developing_on_nuc/nuc_bottom.jpg"/>
   <img width="40%" src="/docs/images/developing_on_nuc/nuc_inside.jpg"/>
1. Install the RAM.
1. Remove the Phillips screws that would hold the SSD in place (a Phillips
   screwdriver with a magnetic tip is useful here).
1. Install the SSD.
1. Mount the SSD in place using the screws from Step 3.

   <img width="40%" src="/docs/images/developing_on_nuc/parts_installed.jpg"/>
1. Put the bottom feet and screws back in.
1. Plug power, ethernet cable, HDMI, and keyboard into the NUC.
1. Plug the other end of the ethernet cable into your workstation
   (or the router or switch connected to the workstation).

## 3. Enable EFI booting {#enable-efi-booting}

To enable EFI (Extensible Firmware Interface) booting on your NUC,
do the following:

1. Reboot your NUC.
1. To enter BIOS, press `F2` while booting.
1. In the **Boot Order** window on the left, click the **Legacy** tab.
1. Uncheck **Legacy Boot**.

   <img width="40%" src="/docs/images/developing_on_nuc/bios.jpg"/>
1. Click the **Advanced** button.
1. Confirm the following boot configuration:
    * Select the **Boot Priority** tab.
       * Check **UEFI Boot**.
       * Set **USB** the first entry in the boot order.
    * Select the **Boot configuration** tab.
       * Check **Boot Network Devices Last**.
       * Check **Unlimited Network Boot Attempts**.
       * Check **USB boot devices**.
       * Set **Network boot** to **UEFI PXE & iSCSI**.
1. Select the **Secure Boot** tab and uncheck **Secure Boot**.
1. To save the changes and exit BIOS, press `F10`.

Note: Network booting only works with the NUC's *built-in* ethernet; netbooting via
USB-ethernet dongle is not supported.

If you want to remotely manage the device, see
[Remote management for NUC][remote-management-for-nuc].

## 4. Build Fuchsia {#build-fuchsia}

To build a Fuchsia system image for your NUC, follow the
[Get started with Fuchsia][get-started-with-fuchsia] guide.

When running `fx set`, make sure to use the `x64` board configuration
(for example, `fx set workstation.x64`).

## 5. Prepare a bootstrap USB drive {#prepare-usb}

Before installing Fuchsia to a device, you need to prepare a bootable USB drive.
On a NUC, Fuchsia boots via a chain of bootloaders. The instructions below creates
a USB drive containing the first two steps in the chain: [Gigaboot][gigaboot] and
[Zedboot][glossary.zedboot].

Gigaboot is a UEFI boot shim with some limited functionality, including
[netbooting][netbooting] and flashing. By default, Gigaboot chains into Zedboot,
which is a bootloader built on top of Zircon. Zedboot then either boots into Fuchsia
or allows you to pave your device. To set up a NUC for the first time, you need to
boot into Zedboot and pave Fuchsia to your device's storage.

To prepare a bootable USB drive, do the following:

1. Plug your USB key into your build workstation.
1. Identify the path to your USB key:

   ```posix-terminl
   fx list-usb-disks
   ```

1. Create a Zedboot USB drive:

   ```posix-terminal
   fx mkzedboot /path/to/usb/disk
   ```

For more information on preparing a bootable USB drive, see
[Prepare a USB flash drive to be a bootable disk][usb-setup].

## 6. Pave Fuchsia {#pave-fuchsia}

To pave Fuchsia on your NUC, do the following:

1. Plug the Zedboot USB key into the NUC and boot it.
1. When Zedboot is started, press `Alt` + `F3` to switch to a command-line prompt.

   Note: If you cannot press `Alt`+`F3` because the keyboard on the NUC is not
   working, see
   [Keyboard not working after Zedboot](#keyboard-not-working-after-zedboot)
   in Troubleshoot.

1. On the NUC, view the HDD or SSD's block device path:

   ```
   lsblk
   ```

   Take note of the block device path (for example, the path might look like
   `/dev/sys/platform/pci/00:17.0/ahci/sata0/block`).

1. On the NUC, clear and initialize the partition tables on the NUC:

   ```
   install-disk-image init-partition-tables --block-device <BLOCK_DEVICE_PATH>
   ```

   Use the block device path from Step 3.

1. On your workstation, install Fuchsia on the NUC:

   ```posix-terminal
   fx pave
   ```

1. After paving is completed, disconnect the USB key.

Fuchsia is now installed on your NUC. When you reboot the machine, it will load Gigaboot,
Zedboot, and Fuchsia all from your device's storage. You don't need the USB drive anymore.
If you need to pave a new version of Fuchsia, you can run `fx reboot -r` on your
workstation, which reboots the NUC into Zedboot.

## Troubleshoot

### Keyboard not working after Zedboot {#keyboard-not-working-after-zedboot}

In the [Pave Fuchsia](#pave-fuchsia) section, after plugging the Zedboot USB
key into the NUC, if you notice that the keyboard on the NUC is not working,
then skip Step 2 through 4 and perform the following workaround instead:

1. On your workstation, try to install Fuchsia on the NUC:

   ```posix-terminal
   fx pave
   ```

   This command may fail due to the partition tables issue on the NUC.

1. View the kernel logs:

   ```posix-terminal
   fx klog
   ```

   In the logs, look for an error message similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   Unable to find a valid GPT on this device with the expected partitions. Please run *one* of the following command(s):
   fx init-partition-tables /dev/sys/platform/pci/00:17.0/ahci/sata0/block
   ```
1. To initialize the partition tables on the NUC, run the suggested command
   in the logs, for example:

   ```none {:.devsite-disable-click-to-copy}
   fx init-partition-tables /dev/sys/platform/pci/00:17.0/ahci/sata0/block
   ```

1. Now, to install Fuchsia on the NUC, run the following command again:

   ```posix-terminal
   fx pave
   ```

<!-- Reference links -->

[remote-management-for-nuc]: nuc-remote-management.md
[get-started-with-fuchsia]: /docs/get-started/README.md
[gigaboot]: /src/firmware/gigaboot
[glossary.zedboot]: /docs/glossary/README.md#zedboot
[netbooting]: /docs/development/kernel/getting_started.md#network-booting
[usb-setup]: /docs/development/hardware/usb_setup.md
