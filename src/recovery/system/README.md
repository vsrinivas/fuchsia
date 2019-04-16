# System Recovery

`system_recovery` is the primary executable in the recovery system. The recovery
system is a second, standalone instance of Fuchsia that runs on devices to
recover the primary Fuchsia system when the primary Fuchsia system is
inoperative.

## Building the recovery image

You can build the recovery image using the following command:

```sh
$ fx build recovery_image
```

This command builds `recovery.zbi`, which is a self-contained archive of the
recovery system.

## Running the recovery image

After building the recovery image, you can run the image in QEMU using the
follosing command:

```sh
$ fx run-recovery -g
```

The easiest way to run recovery on hardware is to netboot a device into a newly
built recovery image:

```sh
$ out/default.zircon/tools/bootserver --board_name pc --boot out/default/recovery.zbi
```
