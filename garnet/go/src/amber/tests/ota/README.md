# OTA Tests

This is the integration tests for Over the Air (OTA) updates. At a high level,
it downloads a package from the package repository, and attempts to update a
device to that version. It verifies a successful OTA by checking if the
/config/build-info/snapshot file is what we expect.

This test is designed to be run against any system that is discoverable by the
zircon tool netaddr. To run, first you will need a running vanilla version of
Fuchsia, either on a device or in QEMU (see below for notes about this). Then,
you will need to build fuchsia with an extra test package that will be included
into the system image. You can do this by running:

```
% fx set x64 \
  --product garnet/products/default.gni \
  --available garnet/packages/tests/system_ota \
  --args 'extra_authorized_keys_file="//.ssh/authorized_keys"'
% fx build
```

Finally, you can run the test with:

```
% ~/fuchsia/out/x64/tools/system_ota_test \
  -fuchsia-build-dir ~/fuchsia/out/x64 \
  -zircon-tools-dir ~/fuchsia/out/build-zircon/tools \
  -builder-name fuchsia/ci/fuchsia-x64-release \
  upgrade
```

This will run through the whole test. There are more options to the test, to see
them all run `~/fuchsia/out/x64/tools/system_ota_test -- -h`.

## Running the tests locally in QEMU

At the moment, the builtin `fx run` does not bring up a QEMU image that can be
OTA-ed. Until this is implemented, the `bin/` directory contains some helper
scripts that bring up an OTA-able QEMU image. Follow these instructions to use
them.

First you need to build or install the [OVMF] UEFI firmware. On Debian, this can
be done with:

```
% apt install ovmf
```

Next, you need to create an EFI Fuchsia image. This can be done by running:

```
% mkdir /tmp/ota-test
% ./bin/make-vol.sh \
  --image /tmp/ota-test/fuchsia-efi.bin
```

This script is just a wrapper around `fx make-fuchsia-vol` to simplify creating
an image that's appropriate for OTA testing.

Finally, run QEMU with:

```
% ./bin/run-qemu.sh \
  --ovmf-dir /usr/share/OVMF \
  --image /tmp/ota-test/fuchsia-efi.bin
```

Note: run-qemu.sh expects to find `OVMF_CODE.fd` and `OVMF_VARS.fd` in the
`--ovmf-dir` directory.

One thing to note, by default, you won't see any terminal output in QEMU after
an OTA. To restore this behavior, you need an extra build argument to send
output to the terminal:

```
% fx set x64 \
  --product garnet/products/default.gni \
  --available garnet/packages/tests/system_ota \
  --args 'extra_authorized_keys_file="//.ssh/authorized_keys"' \
  --args 'kernel_cmdline_args=["kernel.serial=legacy"]'
% fx build
```

With all that setup, you now should be able to test your OTAs.

[OVMF]: https://github.com/tianocore/tianocore.github.io/wiki/OVMF
