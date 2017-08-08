# Hypervisor

The Magenta hypervisor can be used to run a guest operating system. It is a work
in progress.

## Run a guest

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
build-magenta-pc-x86-64/tools/bootserver build-magenta-pc-x86-64/magenta.bin build-magenta-pc-x86-64/bootdata-with-kernel.bin
```

After netbooting the target device, for Magenta run:

```
/boot/bin/guest -r /boot/data/bootdata.bin /boot/data/kernel.bin
```

To boot Linux using an initrd:

```
/boot/bin/guest -r /boot/data/initrd /boot/data/bzImage
```

To boot Linux using a virtio block/root filesystem:

```
# This is optional and only required if you want the block device to be writable.
cp /boot/data/rootfs.ext2 /boot/data/rootfs-rw.ext2

/boot/bin/guest -b /boot/data/rootfs-rw.ext2 -c 'root=/dev/vda rw init=/init' /boot/data/bzImage
```
You should then see the serial output of the guest operating system.
