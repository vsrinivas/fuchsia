# OTA Tests

This is the integration tests for Over the Air (OTA) updates. At a high level,
it extends the current system image by inserting a new unique file into it. The
test then is to trigger an OTA, and verify that file exists in the latest
system.

This test is designed to be run against any system that is discoverable by the
zircon tool netaddr. To run, first you will need a running vanilla version of
Fuchsia, either on a device or in QEMU (see below for notes about this). Then,
you will need to build fuchsia with an extra test package that will be included
into the system image. You can do this by running:

```
% fx set x64 \
  --products garnet/products/default \
  --packages garnet/go/src/amber/tests/ota/ota-test.package
% fx build
```

Finally, you can run the test with:

```
% ./bin/test.sh \
  --fuchsia-dir ~/fuchsia \
  --fuchsia-build-dir ~/fuchsia/out/x64
2018-10-05 17:05:51 [ota-test] starting amber server
serving /usr/local/google/home/etryzelaar/fuchsia/out/x64
2018-10-05 17:05:52 [serve-updates] Device up
2018-10-05 17:05:53 [serve-updates] Registering devhost as update source
2018-10-05 17:05:55 [serve-updates] Ready to push packages!

2018-10-05 17:05:56 [ota-test] verifying fuchsia is running an old version
...
ALL TESTS PASSED
```

This will run through the whole test.

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
  --products garnet/products/default \
  --packages garnet/go/src/amber/tests/ota/ota-test.package \
  --args 'kernel_cmdline_args=["kernel.serial=legacy"]'
% fx build
```

With all that setup, you now should be able to test your OTAs.

[OVMF]: https://github.com/tianocore/tianocore.github.io/wiki/OVMF
