# Install Fuchsia on a NUC

This guide provides instructions on how to install Fuchsia on a
[NUC][nuc-wiki]{:.external} (Next Unit of Computing) device.

The steps are:

1. [Prerequisites](#prerequisites).
1. [Prepare the NUC](#prepare-the-nuc).
1. [Enable EFI booting](#enable-efi-booting).
1. [Bootstrap the NUC](#bootstrap-the-nuc).

## 1. Prerequisites {#prerequisites}

Before you start installing Fuchsia on a NUC device, make sure that
you've completed the following tasks:

Note: The [Build Fuchsia](#build-fuchsia) and
[Prepare a USB drive](#prepare-usb) sections do not require a NUC
device, so you can complete these sections prior to obtaining a NUC device –
however, the Prepare a USB drive section requires a USB flash drive.

* [Get parts](#get-parts).
* [Build Fuchsia](#build-fuchsia).
* [Prepare a USB drive](#prepare-usb).

### Get parts {#get-parts}

The following parts are required for this guide:

Note: Fuchsia only supports the specific system configurations listed in
[Supported system configurations][supported-sys-config].

*  A NUC device (see [example models](#nuc-models))
*  A RAM stick (see [example models](#ram-and-ssd-models))
*  An M.2 SSD stick (see [example models](#ram-and-ssd-models))
*  A USB 3.0 flash drive
*  A keyboard
*  A mouse (Optional)
*  A monitor with an HDMI port
*  An HDMI cable
*  An Ethernet cable
*  A Phillips-head screwdriver (with a magnetic tip)

### Build Fuchsia {#build-fuchsia}

Complete the [Get started with Fuchsia][get-started-with-fuchsia] guide
to set up the Fuchsia development environment on your workstation.

If you have already completed the Get started guide above, do the following:

1. Set your build configuration to `workstation.x64`:

   ```posix-terminal
   fx set workstation.x64
   ```

1.  Build Fuchsia:

    ```posix-terminal
    fx build
    ```

    This generates a Fuchsia image you'll use later in the [Bootstrap the NUC](#bootstrap-the-nuc) section.

### Prepare a USB drive {#prepare-usb}

Installing Fuchsia on a device requires you to prepare a bootable USB drive.

On a NUC, Fuchsia boots the device using a chain of bootloaders. The instructions
in this section creates a bootable USB drive for Fuchsia, which handles the first two
steps in the bootloader chain, [Gigaboot][gigaboot] and [Zedboot][glossary.zedboot].

Gigaboot is a UEFI boot shim with some limited functionality (for instance,
[netbooting][netbooting] and flashing). By default, Gigaboot chains into Zedboot,
which is a bootloader built on top of Zircon. Zedboot then can boot the device
into a Fuchsia product or allow you to pave a Fuchsia image to the device. (For more
information on the bootable USB drive, see
[Prepare a USB flash drive to be a bootable disk][usb-setup].)

To prepare a bootable USB drive, do the following:

Note: The instructions below require that you've completed the [Build
Fuchsia](#build-fuchsia) section above.

1. Plug the USB drive into **your workstation**.
1. Identify the path to the USB drive:

   ```posix-terminal
   fx list-usb-disks
   ```

1. Create a Zedboot-based bootable USB drive:

   ```posix-terminal
   fx mkzedboot <PATH_TO_USB_DRIVE>
   ```

   Replace `PATH_TO_USB_DRIVE` with the path to the USB drive from the step
   above, for example:

   ```none {:.devsite-disable-click-to-copy}
   $ fx mkzedboot /dev/disk2
   ```

   This command creates a Zedboot-based bootable USB drive and ejects the drive.

1. Unplug the USB drive from the workstation.

   You'll need this USB drive later in the [Bootstrap the NUC](#bootstrap-the-nuc) section.

## 2. Prepare the NUC {#prepare-the-nuc}

Some NUC devices do not come with RAM or an SSD. In which case,
you need to install them manually.

<img width="40%" src="/docs/images/developing_on_nuc/parts.jpg"/>

**Figure 1**. A NUC device and RAM and SSD sticks.

To install the RAM and SSD on your NUC, do the following:

1. Remove the Phillips screws on the bottom feet of the NUC.

   <img width="40%" src="/docs/images/developing_on_nuc/nuc_bottom.jpg"/>
   <img width="40%" src="/docs/images/developing_on_nuc/nuc_inside.jpg"/>
1. Install the RAM.
1. Remove the Phillips screws that would hold the SSD in place.

   Note: A Phillips screwdriver with a magnetic tip is useful here.

1. Install the SSD.
1. Mount the SSD in place using the screws from Step 3.

   <img width="40%" src="/docs/images/developing_on_nuc/parts_installed.jpg"/>
1. Put the bottom feet and screws back in.
1. Plug the power, monitor (via HDMI), and keyboard into the NUC.

## 3. Enable EFI booting {#enable-efi-booting}

To enable EFI (Extensible Firmware Interface) booting on your NUC,
do the following:

1. Reboot your NUC.
1. To enter the BIOS setup, press `F2` while booting.
1. In the **Boot Order** window on the left, click the **Legacy** tab.
1. Uncheck **Legacy Boot**.

   <img width="40%" src="/docs/images/developing_on_nuc/bios.jpg"/>
1. Click the **Advanced** button.
1. Confirm the following boot configuration:
    * Under the **Boot Priority** tab:
       * **UEFI Boot** is checked.
    * Under the **Boot Configuration** tab:
       * In the **UEFI Boot** window:
         * **Boot USB Devices First** is checked.
         * **Boot Network Devices Last** is checked.
         * **Unlimited Network Boot Attempts** is checked.
       * In the **Boot Devices** window:
         * **USB** is checked.
         * **Network Boot** is set to **UEFI PXE & iSCSI**.
    * Under the **Secure Boot** tab:
       * **Secure Boot** is unchecked.
1. To save and exit BIOS, press `F10` and click **Yes**.

## 4. Bootstrap the NUC {#bootstrap-the-nuc}

Installing Fuchsia on a device for the first time requires you to boot the device
into Zedboot and pave a Fuchsia image to the device's storage.

To pave Fuchsia on your NUC, do the following:

1. Plug the [Zedboot-based bootable USB drive](#prepare-usb) into the NUC.

1. Connect the NUC directly to the workstation using an Ethernet cable
   (or connect the NUC to a router or WiFi modem in the same
   Local Area Network as the workstation).

   Note: Network booting only works with the NUC's built-in Ethernet port –
   netbooting with an USB port (via an Ethernet-to-USB adapter) is not supported.

1. Reboot your NUC.

   The NUC boots into Fuchsia's Zedboot mode, displaying Zedboot's signature
   blue screen.

1. On the Zedboot screen, press `Alt` + `F3` to switch to a command-line prompt.

   Note: If you cannot press `Alt`+`F3` because the keyboard on the NUC is not
   working, see
   [Keyboard not working after Zedboot](#keyboard-not-working-after-zedboot)
   in Troubleshoot.

1. On the NUC, view the HDD or SSD's block device path:

   ```posix-terminal
   lsblk
   ```

   Take note of the block device path (for example, the path might look like
   `/dev/sys/platform/pci/00:17.0/ahci/sata0/block`).

1. On the NUC, clear and initialize the partition tables of the NUC:

   ```posix-terminal
   install-disk-image init-partition-tables --block-device <BLOCK_DEVICE_PATH>
   ```

   Replace `BLOCK_DEVICE_PATH` with the block device path from the step above,
   for example:

   ```none {:.devsite-disable-click-to-copy}
   $ install-disk-image init-partition-tables --block-device /dev/sys/platform/pci/00:17.0/ahci/sata0/block
   ```

1. **On your workstation**, pave the Fuchsia image to the NUC:

   ```posix-terminal
   fx pave
   ```

1. When the paving is finished, unplug the USB drive from the NUC.

Fuchsia is now installed on your NUC. When you reboot the device, it will load Gigaboot,
Zedboot, and Fuchsia all from your device's storage. So you don't need the USB drive
plugged into the NUC any longer.

Later, if you need to install a new version of Fuchsia (for instance, after re-building
a Fuchsia image using `fx build`), see the
[Flash a new Fuchsia image to the NUC](#flash-fuchsia) section in Appendices.

## Troubleshoot

### Keyboard not working after Zedboot {#keyboard-not-working-after-zedboot}

In the [Bootstrap the NUC](#bootstrap-the-nuc) section, after plugging the
Zedboot USB drive into the NUC, if you notice that the keyboard on the NUC
is not working, then skip Step 4 through 6 and perform the following
workaround instead:

1. **On your workstation**, try to install Fuchsia on the NUC:

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
   $ fx init-partition-tables /dev/sys/platform/pci/00:17.0/ahci/sata0/block
   ```

1. Now, to install Fuchsia on the NUC, run the following command again:

   ```posix-terminal
   fx pave
   ```

### Paving or netbooting not working after Zedboot {#paving-not-working-after-zedboot}

In the [Bootstrap the NUC](#bootstrap-the-nuc) section, after issuing an `fx pave`
command, if paving does not complete, make sure the Ethernet cable
is directly connected to the Ethernet port of the NUC, and is not using an
Ethernet-to-USB adapter to connect to a USB port of the NUC – even though an
Ethernet-to-USB adapter works after Fuchsia has been paved (for instance,
when doing `fx ota`), the USB port doesn't work with Zedboot when paving.

### Address already in use {#address-already-in-use}

In the [Bootstrap the NUC](#bootstrap-the-nuc) section, when you run the `fx pave` command,
you may run into the following error:

```none {:.devsite-disable-click-to-copy}
2022-01-20 15:23:00 [bootserver] cannot bind to [::]:33331 48: Address already in use
there may be another bootserver running
```

When you see this error, do the following:

1. Check the processes that are currently using the port 33331:

   ```posix-terminal
   sudo lsof -i:33331
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ sudo lsof -i:33331
   COMMAND   PID USER   FD   TYPE             DEVICE SIZE/OFF NODE NAME
   ffx     69264 alice  15u  IPv6 0xb12345ed61b7e12d      0t0  UDP *:diamondport
   ```

1. Terminate all the processes in the list, for example:

   ```posix-terminal
   kill 69264
   ```

## Appendices

### NUC models {#nuc-models}

For GPU support, get a NUC7 (Kaby Lake) or NUC8 (Coffee Lake), or a higher
generation.

The list below shows some example models:

 * [Intel® NUC Kit NUC7i5DNKE][NUC7i5DNKE]{:.external}
 * [Intel® NUC Kit NUC7i5DNHE][NUC7i5DNHE]{:.external}
 * [Intel® NUC Kit NUC7i3DNKE][NUC7i3DNKE]{:.external}
 * [Intel® NUC Kit NUC7i3DNHE][NUC7i3DNHE]{:.external}
 * [Intel® NUC Kit NUC8i5BEK][NUC8i5BEK]{:.external}
 * [Intel® NUC Kit NUC8i5BEH][NUC8i5BEH]{:.external}
 * [Intel® NUC Kit NUC8i3BEK][NUC8i3BEK]{:.external}
 * [Intel® NUC Kit NUC8i3BEH][NUC8i3BEH]{:.external}

### RAM and SSD models {#ram-and-ssd-models}

The table below shows some RAM and SSD example models:

| Item | Link | Notes |
| ---- | ---- | ------ |
| RAM | [Crucial 8GB DDR4-2400 SODIMM][ram-01]{:.external} | Works fine. |
| SSD | [Samsung SSD 850 EVO SATA M.2 250GB][ssd-01]{:.external} | Works fine. |
| SSD | [ADATA Ultimate SU800 M.2 2280 3D NAND SSD][ssd-02]{:.external} | Works fine. |
| SSD | [CRUCIAL MX300 SSD][ssd-03]{:.external} | Works fine, but is discontinued. |

### Flash a new Fuchsia image to the NUC {#flash-fuchsia}

Once a NUC is bootstrapped (via [`fx pave`](#bootstrap-the-nuc)) and is running
Fuchsia, you can start using Fuchsia's new flashing process to provision
a new Fuchsia image to the NUC.

Note: The flashing process uses Fuchsia's new [`ffx`][ffx] tool.

To flash a Fuchsia image to your NUC, do the following:

1. Connect the NUC directly to the workstation using an Ethernet cable
   (or connect the NUC to a router or WiFi modem in the same
   Local Area Network as the workstation).

1. Reboot your NUC.

1. To boot the NUC into Fastboot mode, press the `f` key at the Fuchsia boot screen.

   Once the NUC is in Fastboot mode, you can see `entering fastboot mode` printed on the
   screen.

1. **On your workstation**, detect the NUC in Fastboot mode:

   ```posix-terminal
   ffx target list
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ ffx target list
   NAME                      SERIAL       TYPE       STATE       ADDRS/IP                           RCS
   fuchsia-54b2-0389-644b    <unknown>    Unknown    Fastboot    [fe81::55b1:2ff:fe34:567b%en10]    N
   ```

   Verify that the device's state is in `Fastboot`.

1. Flash a new Fuchsia image to the NUC:

   Note: To build a new Fuchsia image, see the [Build Fuchsia](#build-fuchsia) section above.

   ```posix-terminal
   fx flash
   ```

   When the flashing is finished, the NUC reboots and starts running the new
   Fuchsia image.

1. To confirm that the NUC is flashed successfully, run the following command:

   ```posix-terminal
   ffx target list
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ ffx target list
   NAME                      SERIAL       TYPE       STATE       ADDRS/IP                           RCS
   fuchsia-54b2-0389-644b    <unknown>    Unknown    Product    [fe81::55b1:2ff:fe34:567b%en10]    N
   ```

   Notice that the device's state is now `Product`.

<!-- Reference links -->

[nuc-wiki]: https://en.wikipedia.org/wiki/Next_Unit_of_Computing
[remote-management-for-nuc]: nuc-remote-management.md
[get-started-with-fuchsia]: /docs/get-started/README.md
[gigaboot]: /src/firmware/gigaboot
[glossary.zedboot]: /docs/glossary/README.md#zedboot
[netbooting]: /docs/development/kernel/getting_started.md#network-booting
[usb-setup]: /docs/development/hardware/usb_setup.md
[supported-sys-config]: /docs/reference/hardware/support-system-config.md
[NUC7i5DNKE]: https://ark.intel.com/content/www/us/en/ark/products/122486/intel-nuc-kit-nuc7i5dnke.html
[NUC7i5DNHE]: https://ark.intel.com/content/www/us/en/ark/products/122488/intel-nuc-kit-nuc7i5dnhe.html
[NUC7i3DNKE]: https://ark.intel.com/content/www/us/en/ark/products/122495/intel-nuc-kit-nuc7i3dnke.html
[NUC7i3DNHE]: https://ark.intel.com/content/www/us/en/ark/products/122498/intel-nuc-kit-nuc7i3dnhe.html
[NUC8i5BEK]: https://ark.intel.com/content/www/us/en/ark/products/126147/intel-nuc-kit-nuc8i5bek.html
[NUC8i5BEH]: https://ark.intel.com/content/www/us/en/ark/products/126148/intel-nuc-kit-nuc8i5beh.html
[NUC8i3BEK]: https://ark.intel.com/content/www/us/en/ark/products/126149/intel-nuc-kit-nuc8i3bek.html
[NUC8i3BEH]: https://ark.intel.com/content/www/us/en/ark/products/126150/intel-nuc-kit-nuc8i3beh.html
[ram-01]: https://www.crucial.com/memory/ddr4/ct8g4sfs824a
[ssd-01]: https://www.samsung.com/us/computing/memory-storage/solid-state-drives/ssd-850-evo-m-2-250gb-mz-n5e250bw/
[ssd-02]: https://www.adata.com/upload/downloadfile/Datasheet_SU800%20M.2%202280_EN_202003.pdf
[ssd-03]: https://www.crucial.com/products/ssd/mx300-ssd
[ffx]: https://fuchsia.dev/reference/tools/sdk/ffx
