# System Recovery

`system_recovery` is the primary executable in the recovery system. The recovery
system is a second, standalone instance of Fuchsia that runs on devices to
recover the primary Fuchsia system when the primary Fuchsia system is
inoperative.

## Building the recovery image

You can build the recovery image using the following command:

```sh
$ fx build build/images/recovery
```

This command builds several `recovery-*.zbi` files in
`obj/build/images/recovery`, which are self-contained archives of the
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
$ out/default.zircon/tools/bootserver --board_name pc --boot out/default/obj/build/images/recovery/recovery-eng.zbi
```

## Testing

Build the core product with recovery:

```sh
$ fx set core.x64 --with //src/recovery
$ fx build
```

The system you've just built onto your device.

### Unit

Run the recovery integration test:

```sh
$ fx run-test system_recovery_tests
```

### Integration

Run the recovery integration test:

```sh
$ fx run-test recovery_integration_test
```

### End-to-end

TODO: Need to create end-to-end tests.
