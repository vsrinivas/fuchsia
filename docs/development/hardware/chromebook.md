# Install Fuchsia on a Chromebook

This guide provides instructions on how to install Fuchsia on a Chromebook
device.

## Supported Chromebooks {:#supported-chromebooks}

Google Pixelbook Go (Atlas) is supported and regularly tested by Fuchsia developers.
Some x86-based ChromeOS devices in the market may work with the instructions in
this guide.

However, the following Chromebooks are not supported:

* Google Pixelbook (Eve)
* ARM-based ChromeOS devices

## Prerequisites {:.numbered}

This guide requires that you build a `chromebook-x64` product in the Fuchsia
source development environment and create a bootable USB flash drive:

1. Complete the [Download the Fuchsia source code][get-fuchsia-source]
   guide.
2. As part of [Configure and Build Fuchsia][build-fuchsia], set your build
   configuration to the following Chromebook product:

   ```posix-terminal
   fx set workstation_eng.chromebook-x64 --release
   ```

3. To create a bootable USB drive, follow the instructions in the
   [Install Fuchsia from a USB flash drive][install-fuchsia-from-usb]
   guide. However, **stop** after creating the bootable USB drive and
   return to this guide.

   Once the USB drive is created, follow the steps below to set up
   your Chromebook to boot from the USB drive.

## Update ChromeOS {:#update-chromeos .numbered}

If your Chromebook has never been booted, boot it normally to check for updates:

1. Boot the Chromebook normally.
1. Click the **Let's go** button.
1. Connect to a wired or wireless network.
1. Accept the terms to proceed to check for updates.
1. If any updates are found, install them.
1. After rebooting from updates, click **Browse as Guest**.

   From the browser UI, you go to **Settings->About ChromeOS** or
   **Help->About ChromeOS** to confirm the newly installed version.

## Boot Chromebook into Developer Mode {:#boot-chromebook-into-developer-mode .numbered}

Caution: This will erase any state stored locally on your Chromebook.

To put your Chromebook into Developer Mode, do the following:

1. Power off the Chromebook.
1. To enter Recovery Mode, hold down `Esc+Refresh` (first and third buttons on
   the top row of the keyboard) and press the power button.
1. Press `Ctrl+D` to disable OS verification.

1. When you see "To turn OS verification OFF, press ENTER", press `Enter` to confirm.

   When your device reboots, you get a confirmation that says "OS verification is OFF."

1. Press `Ctrl+D` again to enter Developer Mode.

   Wait for the device to reconfigure itself, which will take several minutes.
   Initially it may not appear to be doing anything. Let the device sit for a
   minute or two. You will hear two loud beeps early in the process. The
   process is complete when you hear two more loud beeps.

   The device reboots itself when the Developer Mode transition is complete. You can
   now go to the next [Boot Chromebook from USB](#boot-chromebook-from-usb) section.

## Boot Chromebook from USB {:#boot-chromebook-from-usb .numbered}

To boot your Chromebook from USB, do the following:

1. Boot into ChromeOS in Developer Mode.

   The screen says "OS verification is OFF." Approximately 30 seconds later the boot
   will continue. Wait for the welcome or login screen to load. **Ignore** the link for
   "Enable debugging features".

1. Press `Ctrl+Alt+Refresh(F3)` to enter a command shell.

   If pressing this key combination has no effect, try rebooting the Chromebook once again.

1. Enter `chronos` as the user with a blank password.
1. To enable USB booting, run the following command on the command shell:

   ```posix-terminal
   sudo crossystem dev_boot_usb=1
   ```

1. (**Optional**) To set USB booting to be the default, run the following command:

   ```posix-terminal
   sudo crossystem dev_default_boot=usb
   ```

1. Plug the [bootlable USB drive][install-fuchsia-from-usb] into the Chromebook.
1. To reboot the device, run the following command:

   ```posix-terminal
   sudo reboot
   ```

1. On the "OS verification is OFF" screen, to bypass the timeout, press `Ctrl+U` to boot
   from USB immediately.

   After booting from USB, the Chromebook starts the Fuchsia installer.

1. Press `Enter` on prompts to continue the installation process.

   When the installation is finished, the screen displays "Success! Please restart your computer."

1. Unplug the USB drive from the Chromebook.

   From this point, The USB drive is only needed for booting when you want to re-pave or
   netboot your Chromebook.

1. Reboot the Chromebook.

   **Your Chromebook is now booted into Fuchsia!**

## Appendices

### Bypassing a long wait from the boot screen

By default the ChromeOS bootloader has a long timeout to allow you to press
buttons. To shortcut this you can press `Ctrl+D` or `Ctrl+U` when on the grey screen
that warns that the OS will not be verified. `Ctrl+D` will cause the device to
skip the timeout and boot from its default source. `Ctrl+U` will skip the timeout
and boot the device from USB.

### Booting from USB

If you didn't make USB booting the default in the step 5 of the
[Boot Chromebook from USB](#boot-chromebook-from-usb) section, you will need to press `Ctrl+U`
at the grey "warning OS-not verified" screen to boot from USB when you power on your device.

If the device tries to boot from USB, either because that is the default or you
pressed `Ctrl+U`. If the device fails to boot from USB, you'll hear a loud beep.

Note that ChromeOS bootloader USB enumeration during boot has been observed to be slow.
If you're having trouble booting from USB, it may be helpful to remove other USB devices
until the device is through the bootloader and also avoid using a USB hub.

### Configuring boot source from Fuchsia

Fuchsia has an equivalent to `crossystem` called `cros_nvtool`.
You can run `cros_nvtool set dev_boot_default <usb|disk>` to modify the default boot source of
the system to USB or disk, respectively.

### Going back to ChromeOS

To go back to ChromeOS, you must modify the priority of the Fuchsia kernel
partition to be lower than that of at least one of the two ChromeOS kernel
partitions:

1. Press `Alt+Esc` to get to a virtual console if not already on one.
1. Press `Alt+Fullscreen` to get to a terminal emulator on Fuchsia.
1. Find the disk that contains the `KERN-A`, `KERN-B`, and `KERN-C` partitions with
   the `lsblk` command. In the example below, `000` is the target disk – note how the
   device path of the kernel partitions is an extension of that device:

   ```none {:.devsite-disable-click-to-copy}
   $ lsblk
   ID  SIZE TYPE             LABEL                FLAGS  DEVICE
   000 232G                                              /dev/sys/platform/pci/00:1e.4/pci-sdhci/sdhci/sdmmc/block
   001   5G data             STATE                       /dev/sys/platform/pci/00:1e.4/pci-sdhci/sdhci/sdmmc/block/part-000/block
   002  16M cros kernel      KERN-A                      /dev/sys/platform/pci/00:1e.4/pci-sdhci/sdhci/sdmmc/block/part-001/block
   003   4G cros rootfs      ROOT-A                      /dev/sys/platform/pci/00:1e.4/pci-sdhci/sdhci/sdmmc/block/part-002/block
   004  16M cros kernel      KERN-B                      /dev/sys/platform/pci/00:1e.4/pci-sdhci/sdhci/sdmmc/block/part-003/block
   005   4G cros rootfs      ROOT-B                      /dev/sys/platform/pci/00:1e.4/pci-sdhci/sdhci/sdmmc/block/part-004/block
   006  64M cros kernel      KERN-C                      /dev/sys/platform/pci/00:1e.4/pci-sdhci/sdhci/sdmmc/block/part-005/block
   007   4G cros rootfs      ROOT-C                      /dev/sys/platform/pci/00:1e.4/pci-sdhci/sdhci/sdmmc/block/part-006/block
   ```

1. Use the `gpt` command to look at the device's (that is, `000` in the example
   above) partition map:

   ```none {:.devsite-disable-click-to-copy}
   $ gpt dump /dev/class/block/000
   blocksize=0x200 blocks=488554496
   Partition table is valid
   GPT contains usable blocks from 34 to 488554462 (inclusive)
   Partition 0: STATE
       Start: 478035968, End: 488521727 (10485760 blocks)
       id:   51E8D442-0419-2447-96E5-49CB60CF0B25
       type: EBD0A0A2-B9E5-4433-87C0-68B6B72699C7
       flags: 0x0000000000000000
   Partition 1: KERN-A
       Start: 20480, End: 53247 (32768 blocks)
       id:   054CD627-F23C-5C40-8035-C188FA57DE9C
       type: FE3A2A5D-4F32-41A7-B725-ACCC3285A309
       flags: priority=2 tries=0 successful=1
   Partition 2: ROOT-A
       Start: 8704000, End: 17092607 (8388608 blocks)
       id:   936E138F-1ACF-E242-9C5B-3667FAA3C10C
       type: 3CB8E202-3B7E-47DD-8A3C-7FF2A13CFCEC
       flags: 0x0000000000000000
   Partition 3: KERN-B
       Start: 53248, End: 86015 (32768 blocks)
       id:   A8667891-8209-8648-9D5E-63DC9B8D0CB3
       type: FE3A2A5D-4F32-41A7-B725-ACCC3285A309
       flags: priority=1 tries=0 successful=1
   Partition 4: ROOT-B
       Start: 315392, End: 8703999 (8388608 blocks)
       id:   8B5D7BB4-590B-E445-B596-1E7AA1BB501F
       type: 3CB8E202-3B7E-47DD-8A3C-7FF2A13CFCEC
       flags: 0x0000000000000000
   Partition 5: KERN-C
       Start: 17092608, End: 17223679 (131072 blocks)
       id:   C7D6B203-C18F-BC4D-9160-A09BA8970CE1
       type: FE3A2A5D-4F32-41A7-B725-ACCC3285A309
       flags: priority=3 tries=15 successful=1
   Partition 6: ROOT-C
       Start: 17223680, End: 25612287 (8388608 blocks)
       id:   769444A7-6E13-D74D-B583-C3A9CF0DE307
       type: 3CB8E202-3B7E-47DD-8A3C-7FF2A13CFCEC
       flags: 0x0000000000000000
   ```

   Note that `KERN-A` and `KERN-B` typically have ChromeOS kernels. The
   Zircon kernel appears as `KERN-C` as in the example above, or as
   `ZIRCON-A` instead in certain setups.

1. To go to ChromeOS, lower the priority of `KERN-C` (or `ZIRCON-A`)
   by referencing the partition index on the disk that has that partition,
   for example:

   ```none {:.devsite-disable-click-to-copy}
   $ gpt edit_cros 5 -P 0 /dev/class/block/000
   ```

1. Reboot.

1. When the ChromeOS bootloader appears, press `Space` to re-enable
   OS Verification.

   Your device will reboot. This time, it will display a message with "Your
   system is repairing itself. Please wait." This operation will take around
   5 minutes, after which the Chromebook will reboot one final time. The device
   will reboot to the initial setup screen.

   From here, if you want to return to the Fuchsia kernel, you can just
   re-pave the Chromebook.

<!-- Reference links -->

[get-fuchsia-source]: /docs/get-started/get_fuchsia_source.md
[build-fuchsia]: /docs/get-started/build_fuchsia.md
[install-fuchsia-from-usb]: /docs/development/hardware/usb_setup.md
