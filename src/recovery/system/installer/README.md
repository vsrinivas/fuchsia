# Recovery Workstation Installer

`system_recovery_installer` is an executable for the fuchsia workstation installer 
from the recovery system.

## Building the workstation installer

You can build the recovery workstation isntaller image using the following command:

```sh
$ fx build build/images/recovery
```

This command builds an additional `recovery-installer.zbi` files in
`obj/build/images/recovery/recovery-installer`

## Running the installer

A method of running the workstation installer on hardware is to netboot a device into a newly
built recovery image:

```sh
$ out/default/host-tools/bootserver --board-name device-name --boot out/default/obj/build/images/recovery/recovery-installer/recovery-installer.zbi
```
where `device-name` can be found with `fx list-devices`.

**NB** If you only have one device or have used the `fx set-device` command
you can omit the `--board-name` argument.

## Testing

Build the core product with recovery:

```sh
$ fx set core.x64 --with //src/recovery
$ fx build
```

Load the system you've just built onto your device.

### Unit

The installer has a separate set of unit tests which can be run with:

```sh
$ fx test -s 5 system_installer_tests
```

Note that test_ui requires display access, so using a product other than "core"
will likely cause these tests to fail.

### Integration

TODO: Need to create integration tests.

### End-to-end

TODO: Need to create end-to-end tests.
