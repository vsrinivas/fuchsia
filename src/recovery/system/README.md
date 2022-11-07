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

The easiest way to run recovery on hardware is to boot the newly built
recovery image with fastboot:

```sh
$ cat out/default/obj/build/images/recovery/recovery-eng/recovery-eng.{zbi,vbmeta} > \
      out/default/obj/build/images/recovery/recovery-eng/recovery-eng.boot
$ fastboot boot out/default/obj/build/images/recovery/recovery-eng/recovery-eng.boot
```

Alternatively, you can netboot a device into a newly built recovery image:

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

### Providing Omaha config values in vbmeta ###

When you need to test against an Omaha server you can include arguments in a custom zircon_r.vbmeta.

1. Create a property file with any of the following arguments to set default values:
```
$ tee out/default/recovery-omaha.props <<EOF
ota_channel=stable
omaha_app_id=example_product:abcdefghijklmno0123456789_userdebug
omaha_url=https://clients2.google.com/service/update2/fuchsia/json
EOF
```
2. Wrap properties in a zbi
```
out/default/host_x64/zbi --output out/default/recovery-omaha.zbi --type IMAGE_ARGS out/default/recovery-omaha.props
```
3. Create custom vbmeta file.\
Note the args `--key`, `--algorithm`, and `--public_key_metadata` need to match the args used for your board (set in `boards/<board-name>.gni` as `avb_key`, `avb_algorithm`, and `avb_atx_metadata` respectively).\
The arg `--include_descriptors_from_image` should point to the vbmeta file you want to add omaha args to.
```
${FUCHSIA_DIR}/third_party/android/platform/external/avb/avbtool \
make_vbmeta_image \
--output out/default/recovery-omaha-args.vbmeta \
--key src/firmware/avb_keys/vim3/vim3-dev-key/vim3_devkey_atx_psk.pem \
--algorithm SHA512_RSA4096 \
--public_key_metadata src/firmware/avb_keys/vim3/vim3-dev-key/vim3_dev_atx_metadata.bin \
--include_descriptors_from_image out/default/obj/build/images/recovery/recovery-eng/recovery-eng.vbmeta \
--prop_from_file zbi_image_args:out/default/recovery-omaha.zbi
```
4. Flash custom vbmeta file
```
fx fastboot flash zircon_r out/default/recovery-omaha-args.vbmeta
```

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
