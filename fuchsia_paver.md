# Putting Fuchsia on a Device

One of the best ways to experience Fuchsia is by running it on actual hardware.
This guide will help you get Fuchsia installed on your device. Fuchsia has good
support for a few different hardware platforms including the Acer Switch 12,
Intel NUC, and Google Pixelbook (not to be confused with the Chromebook Pixel).
The install process is not currently compatible with ARM-based targets. The
Fuchsia install process, called 'paving', requires two machines, the machine on
which you want to run Fuchsia ("target") and the machine on which you build
Fuchsia ("host"). Host and target must be able to communicate over a local area
network. On your host system you will build Fuchsia, create a piece of install
media, and stream a large portion of the system over the network to the target.

The `fx` command will be used throughout these instructions. If you have fx
mapped into your command path you can follow the instructions verbatim. If you
don't have fx in your path, it can be found at `//scripts/fx` and you'll need
to use the appropriate relative path in the supplied commands. Many of fx
commands are relatively thin wrappers around build actions in GN coupled with
tool invocations. If your use case isn't quite served by what's currently
available there may a few GN targets you can build or some GN templates you can
extend to allow you to build what you need.

## TL;DR

Read this all before? Here are the common case commands
1. `fx set x86-64`
2. `fx full-build`
3. Make the install media
    * [[ insert USB drive into host ]]
    * `fx mkzedboot <usb_drive_device_path>`
4. Boot and pave
    * [[ move USB drive to target ]]
    * `fx boot-paver <efi|vboot|nuc|cros|..>`

## Building

Detailed instructions for obtaining and building Fuchsia are available from the
[Getting Started](getting_started.md) guide, but we'll assume here that the
target system is x86-based and that you want to build a complete system. To
configure our build for this we can run `fx set x86-64` and then build with
`fx full-build`.

## Creating install media

To create your install media we recommend using a USB drive since these are
well-supported as boot media by most systems. Note that the install media
creation process **will wipe everything** from the USB drive being used. Insert the
USB drive and then run `fx mkzedboot <device_path>`, which on Linux is
typically something like /dev/sd&lt;X&gt; where X is a letter and on Mac is typically
something like /dev/disk&lt;N&gt; where 'N' is a number. **Be careful not to select
the wrong device**. Once this is done, remove the USB drive.

## Paving

Now we'll build the artifacts to transfer over the network during the paving
process. What is transferred is dependent on the target device. For UEFI based
systems (like Intel NUC or Acer Switch 12) our output target type is 'efi'. For
ChromeOS-based systems (like Pixelbook) that use vboot-format images the target
type is 'vboot'. To build our output set you can run
`fx boot-paver <target_type>`, ie. `fx boot-paver efi`.

Insert the install media into the target device that you want to pave. The target
device's boot settings may need to be changed to boot from the USB device and
this is typically device-specific. For the guides provided here, **only** go
through the steps to set the boot device, don't continue with any instructions on
creating install media.
* [Acer Switch Alpha 12](https://fuchsia.googlesource.com/zircon/+/master/docs/targets/acer12.md)
* [Intel NUC](https://fuchsia.googlesource.com/zircon/+/master/docs/targets/nuc.md)
* [Google Pixelbook](hardware/pixelbook.md)

Paving should occur automatically after the device is booted into Zedboot from the
USB drive. After the paving process completes the system should boot into the
Zircon kernel. After paving, the whole system is installed on storage. At this
point the USB key can be removed since the system has everything it needs stored
locally. If you plan to re-pave frequently it may be useful to keep the
USB drive inserted so your system boots into Zedboot by default where paving
will happen automatically. After the initial pave on UEFI systems that use
Gigaboot, another option for re-paving is to press 'z' while in Gigaboot to
select Zedboot. For vboot-based systems using the USB drive is currently the
only option for re-paving. In all cases the bootserver needs to have been
started with `fx boot-paver <target_type>`

## Troubleshooting

In some cases paving may fail because you have a disk layout that is incompatible.
In these cases you will see a message that asks you to run
'install-disk-image wipe'. If it is incompatible because it contains an older
Fuchsia layout put there by installer (vs the paver) you can fix this by killing
the boot-paver process on the host, switching to a different console (Alt+F3) on
the target, and running `install-disk-image wipe`. Then reboot the target,
re-run `fx boot-paver <target_type>` on the host, and the pave should succeed.

## Running without a persistent /data partition

It is possible to run the system without a persistent data partition. When this is
done /data is backed by a RAM filesystem and thus the device is kind of running in
an incognito mode. To create a device without a persistent data partition you can
start the boot-paver with the '--no-data' option, for example
`fx boot-paver <target_type> --no-data`. If the device already has a data
partition, running paver this way will **not** remove it. To remove the persistent
data partition, don't run bootserver, boot the device into Zedboot, switch to a
command line (Alt+F3), and run `install-disk-image wipe`. Then reboot the
device into Zedboot and start boot server with
`fx boot-paver <target_type> --no-data`.

## Changing boot target (localboot, netboot, etc) default

For EFI-based systems, it is possible to change the default boot option of the
system paved on the target between local booting and Zedboot for network booting.
By default the system boots locally with a 1-second delay in Gigaboot to allow you
to select a different mode. To change this default to Zedboot, supply the
`--always_zedboot` option when calling your build command, for example
`fx full-build --always_zedboot`.
