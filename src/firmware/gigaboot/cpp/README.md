# Gigaboot in C++

This folder contains source code for Fuchsia Gigaboot written in C++. It is built
using the physboot toolchain defined in `zircon/kernel/phys/efi/BUILD.gn`. The code
is currently under construction. It'll eventually replace the C gigaboot
implementation in the parent folder (b/235489025).

A gn target `src/firmware/gigaboot/cpp:esp` is added for producing a bootable
UEFI image that can be flashed to the device. To build the target, follow steps:

```
fx set workstation.x64 --with //src/firmware/gigaboot/cpp:esp
fx build
```

The output path of the image is `<build_out_dir>/gigaboot_cpp.esp.blk`. Until
fastboot is implemented, the following gives one way to flash the image to NUC
for testing/development.

1. Follow instructions in [Install Fuchsia on a NUC] to prepare a bootable USB
with the Fuchsia installer image and bootstrap NUC for the first time. The
USB Fuchsia installer image has a userspace fastboot over tcp component that
we'll use to flash gigaboot. This only needs to be done once.

2. Plug in the bootable USB and power on NUC. Find out the ip address of the
device from the serial log or via `ffx target list`.

3. Run `fastboot flash fuchsia-esp <gigaboot image> -s tcp:<ip address>`

4. Unplug the USB and power cycle the device. The device will now run the new
gigaboot image.

Repeat step 2-4 for future flash.

[Install Fuchsia on a NUC]: /docs/development/hardware/intel_nuc.md
