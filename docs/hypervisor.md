# Hypervisor

The Magenta hypervisor can be used to run a guest operating system. It is a work
in progress.

## Running a guest

To run a guest using the hypervisor, you must create a bootfs image containing
the guest and use the `guest` app to launch it.

Note: `guest` only supports the Magenta and Linux kernels.

On your host device, from the Magenta directory, run:
```
scripts/build-magenta-x86-64

# Optional: Build Linux, an initial RAM disk, and an EXT2 file-system.
system/uapp/guest/scripts/mklinux.sh
system/uapp/guest/scripts/mktoybox.sh -ri

# Optional: Build a GPT disk image for Magenta guests.
system/uapp/guest/scripts/mkgpt.sh

system/uapp/guest/scripts/mkbootfs.sh
build-magenta-pc-x86-64/tools/bootserver \
    build-magenta-pc-x86-64/magenta.bin \
    build-magenta-pc-x86-64/bootdata-with-guest.bin
```

### Magenta guest

After netbooting the target device, to run Magenta:
```
/boot/bin/guest -r /boot/data/bootdata.bin /boot/data/magenta.bin
```

To run Magenta using a GPT disk image:
```
/boot/bin/guest \
    -b /boot/data/magenta.gpt \
    -r /boot/data/bootdata.bin \
    /boot/data/magenta.bin
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
