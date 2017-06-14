# Hypervisor

The Magenta hypervisor can be used to run a guest operating system. It is a work
in progress.

## Run a guest

To run Magenta as a guest using the hypervisor, you must create a bootfs image
containing the guest, and use the `guest` app to launch the guest.

On your host device, from the Magenta directory, run:

```
scripts/build-magenta-x86-64
system/uapp/guest/scripts/mkbootfs.sh
build-magenta-pc-x86-64/tools/bootserver build-magenta-pc-x86-64/magenta.bin build-magenta-pc-x86-64/bootdata-with-kernel.bin
```

After netbooting the target device, run:

```
/boot/bin/guest /boot/data/kernel.bin /boot/data/bootdata.bin
```

You should then see the serial output of the guest operating system.
