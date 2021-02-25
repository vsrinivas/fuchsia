# Live_usb

This component can be included in a build to support writing a sparse FVM to a
a ramdisk during boot, which results in a temporary FVM which is lost once the
system is rebooted.

This enables trying Fuchsia without worrying about paving a device or
sacrificing a computer to try it on.

## Using it

1. Set up a Fuchsia development environment.
2. Run the following in a shell:

  ```
  $ echo 'boot.usb=true bootloader.default=local devmgr.bind-eager=usb_composite' > cmdline
  $ fx set workstation.x64 && fx build
  ```

3. Insert a USB device. We will call it `/dev/sdX` - replace X with the
   appropriate drive letter.
4. Run the following:
  ```
  $ fx make-fuchsia-vol -use-sparse-fvm -cmdline cmdline /dev/sdX
  ```
5. Insert the USB drive into a Fuchsia-compatible computer, boot from it, and wait a minute or two.
