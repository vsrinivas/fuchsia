# System Tests

This is the integration tests for a number of system tests.

## Test Setup

In order to build the system tests, add this to your `fx set`:

```
% fx set ... --with //src/sys/pkg:tests
% fx build
```

Next, you need to authenticate against luci to be able to download build
artifacts. Install chromium's `depot_tools` by following
[these instructions](https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html).
Then, login to luci by running:

```
% cd depot_tools
% ./luci-auth login -scopes "https://www.googleapis.com/auth/devstorage.read_write"
...
```

Now you should be able to run the tests against any device that is discoverable
by the zircon tool `netaddr`.

## Available Tests

### Upgrade Tests

This tests Over the Air (OTA) updates. At a high level, it:

* Downloads downgrade and upgrade build artifacts
* Tells the device to reboot into recovery.
* Paves the device with the downgrade build artifacts (known as version N-1).
* OTAs the to the upgrade build artifacts (known as version N).
* OTAs the device to a variant of the upgrade build artifacts (known as version
  N').
* OTAs back into upgrade build artifacts (version N).

You should be able to run the tests with:

```
% ~/fuchsia/out/default/host_x64/system_tests_upgrade \
  -ssh-private-key ~/fuchsia/.ssh/pkey \
  -downgrade-builder-name fuchsia/ci/fuchsia-x64-release \
  -upgrade-fuchsia-build-dir $FUCHSIA_BUILD_DIR
```

This will run through the whole test paving the build to the latest version
available from the specified builder, then OTA-ing the device to the local build
directory.

The Upgrade Tests also support reproducing a specific build. To do this,
determine the build ids from the downgrade and upgrade builds, then run:

```
% ~/fuchsia/out/default/host_x64/system_tests_upgrade \
  -ssh-private-key ~/fuchsia/.ssh/pkey \
  -downgrade-build-id 123456789... \
  -upgrade-build-id 987654321...
```

Or you can combine these options:

```
% ~/fuchsia/out/default/host_x64/system_tests_upgrade \
  -ssh-private-key ~/fuchsia/.ssh/pkey \
  -downgrade-build-id 123456789... \
  -upgrade-fuchsia-build-dir $FUCHSIA_BUILD_DIR
```

There are more options to the test, to see them all run
`~/fuchsia/out/default/host_x64/system_tests_upgrade -- -h`.

### Reboot Testing

The system tests support running reboot tests, where a device is rebooted a
configurable number of times, or errs out if a problem occurs. This
can be done by running:

```
% ~/fuchsia/out/default/host_x64/system_tests_reboot \
  -ssh-private-key ~/fuchsia/.ssh/pkey \
  -builder-name fuchsia/ci/fuchsia-x64-release
```

### Tracking Testing

The system tests support running tracking tests, where a device is
continuously updated to the latest available version, or errs out if a problem
occurs. This can be done by running:

```
% ~/fuchsia/out/default/host_x64/system_tests_tracking \
  -ssh-private-key ~/fuchsia/.ssh/pkey \
  -downgrade-builder-name fuchsia/ci/fuchsia-x64-release \
  -upgrade-builder-name fuchsia/ci/fuchsia-x64-release
```

The `-downgrade-build*` argument is optional, and only necessary if you want to
start the tracking test from a known zero state.

Note that at the moment the only supported upgrade mode is
`-upgrade-builder-name $BUILDER_NAME`.

## Running the tests locally in QEMU (experimental)

At the moment, the builtin `fx emu` does not bring up a configuration that can be
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
% fx set ... \
  --with //src/sys/pkg:tests \
  --args 'kernel_cmdline_args=["kernel.serial=legacy"]'
% fx build
```

With all that setup, you now should be able to run the tests.

[OVMF]: https://github.com/tianocore/tianocore.github.io/wiki/OVMF
