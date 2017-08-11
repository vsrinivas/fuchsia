# Hypervisor

The Magenta hypervisor can be used to run a guest operating system. It is a work
in progress.

## Running a guest

To run a guest using the hypervisor, you must create a bootfs image
containing the guest, and use the `guest` app to launch it.

`guest` currently supports Magenta and Linux kernels.

On your host device, from the Magenta directory, run:

```
scripts/build-magenta-x86-64

# Optional Linux steps to build the kernel and a simple userspace.
system/uapp/guest/scripts/mklinux.sh
system/uapp/guest/scripts/mktoybox.sh -ri

system/uapp/guest/scripts/mkbootfs.sh
build-magenta-pc-x86-64/tools/bootserver \
    build-magenta-pc-x86-64/magenta.bin \
    build-magenta-pc-x86-64/bootdata-with-guest.bin
```

After netbooting the target device, to run Magenta:

```
/boot/bin/guest -r /boot/data/bootdata.bin /boot/data/magenta.bin
```

To run Linux using an initrd:

```
/boot/bin/guest -r /boot/data/initrd /boot/data/bzImage
```

To run Linux using a *read-only* Virtio-block root file-system:

```
/boot/bin/guest -b /boot/data/rootfs.ext2 -c 'root=/dev/vda ro init=/init' /boot/data/bzImage
```

To run Linux using a *writable* Virtio-block root file-system:

```
cp /boot/data/rootfs.ext2 /boot/data/rootfs-rw.ext2

/boot/bin/guest -b /boot/data/rootfs-rw.ext2 -c 'root=/dev/vda rw init=/init' /boot/data/bzImage
```

You should then see the serial output of the guest operating system.
