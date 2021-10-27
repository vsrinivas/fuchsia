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
following command:

```sh
$ fx run-recovery -g
```

The easiest way to run recovery on hardware is to netboot a device into a newly
built recovery image:

```sh
$ out/default/host-tools/bootserver --board-name device-name --boot out/default/obj/build/images/recovery/recovery-eng/recovery-eng.zbi
```
where `device-name` can be found with `fx list-devices`.

**NB** If you only have one device or have used the `fx set-device` command
you can omit the `--board-name` argument.

## Flash and shell into the recovery image

After building the recovery image, you can flash it via the following commands:

```sh
fastboot erase vbmeta_a && \
  fastboot erase vbmeta_b && \
  fastboot flash vbmeta_r recovery-eng.vbmeta && \
  fastboot flash zircon_r recovery-eng.zbi && \
  fastboot stage /home/fuchsia/.ssh/authorized_keys oem add-staged-bootloader-file ssh.authorized_keys && \
  fastboot continue
```

The above block will disable the A/B slots so that we default into recovery,
provisions the R slot with recovery-eng, then stages the SSH authorized keys for
recovery-eng to enable SSH access. The device can then be accessed via the
standard ssh-based fx commands like `fx shell`.

## Testing

Build the core product with recovery:

```sh
$ fx set core.x64 --with //src/recovery
$ fx build
```

Load the system you've just built onto your device.

### Unit

Run the system recovery unit tests:

```sh
$ fx test -s 5 system_recovery_tests
```

Note that test_ui requires display access, so using a product other than "core"
will likely cause these tests to fail.

### Integration

Run the recovery integration test:

```sh
$ fx test -s 5 recovery_integration_test
```

### End-to-end

TODO: Need to create end-to-end tests.
