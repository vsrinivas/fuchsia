# Hypervisor

The Zircon hypervisor can be used to run a guest operating system. It is a work
in progress.

These instructions will guide you through creating minimal Zircon and Linux
guests. For instructions on building a more comprehensive linux guest system
see [Hypervisor Benchmarking](hypervisor/benchmarking.md).

## Running a guest

To run a guest using the hypervisor, you must create a bootfs image containing
the guest and use the `guest` app to launch it.

Note: `guest` only supports the Zircon and Linux kernels.

On your host device, from the Zircon directory, run:
```
scripts/build-zircon-x86-64

# Optional: Build Linux, an initial RAM disk, and an EXT2 file-system.
system/uapp/guest/scripts/mklinux.sh
system/uapp/guest/scripts/mksysroot.sh -ri

# Optional: Build a GPT disk image for Zircon guests.
system/uapp/guest/scripts/mkgpt.sh

system/uapp/guest/scripts/mkbootfs.sh
build-zircon-pc-x86-64/tools/bootserver \
    build-zircon-pc-x86-64/zircon.bin \
    build-zircon-pc-x86-64/bootdata-with-guest.bin
```

### Zircon guest

After netbooting the target device, to run Zircon:
```
/boot/bin/guest -r /boot/data/bootdata.bin /boot/data/zircon.bin
```

To run Zircon using a GPT disk image:
```
/boot/bin/guest \
    -b /boot/data/zircon.gpt \
    -r /boot/data/bootdata.bin \
    /boot/data/zircon.bin
```

### Linux guest

After netbooting the target device, to run Linux using an initial RAM disk:
```
/boot/bin/guest -r /boot/data/initrd /boot/data/bzImage
```

To run Linux using a **read-only** EXT2 root file-system:
```
/boot/bin/guest \
    -b /boot/data/rootfs.ext2 \
    -c 'root=/dev/vda ro init=/init' \
    /boot/data/bzImage
```

To run Linux using a **writable** EXT2 root file-system:
```
cp /boot/data/rootfs.ext2 /boot/data/rootfs-rw.ext2

/boot/bin/guest \
    -b /boot/data/rootfs-rw.ext2 \
    -c 'root=/dev/vda rw init=/init' \
    /boot/data/bzImage
```

Linux also supports an interactive graphical framebuffer:

```
/boot/bin/guest \
    -g \
    -r /boot/data/initrd \
    -c 'console=tty0' \
    /boot/data/bzImage
```

This will cause the guest to gain control of the framebuffer. Keyboard HID
events will be passed through to the guest using a virito-input device
(automatically enabled when the GPU is enaled with `-g`). You can
toggle back to the host virtcon by pressing Alt+Esc.
