# System Tests

This is the integration tests for a number of system tests.

## Test Setup

In order to build the system tests, add this to your `fx set`:

```
% fx set ... --with //src/sys/pkg:e2e_tests
% fx build
```

Next, you need to authenticate against luci to be able to download build
artifacts. Install chromium's `depot_tools` by following
[these instructions](https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html).
Then, login to luci by running:

```
% cd depot_tools
% ./luci-auth login -scopes "https://www.googleapis.com/auth/userinfo.email https://www.googleapis.com/auth/devstorage.read_write"
...
```

Now you should be able to run the tests against any device that is discoverable
by `fx device-finder`.

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
% $(fx get-build-dir)/host_x64/system_tests_upgrade \
  --ssh-private-key ~/.ssh/fuchsia_ed25519 \
  --downgrade-builder-name fuchsia/global.ci/fuchsia-x64-release-build_only \
  --upgrade-fuchsia-build-dir $FUCHSIA_BUILD_DIR
```

This will run through the whole test paving the build to the latest version
available from the specified builder, then OTA-ing the device to the local build
directory.

The Upgrade Tests also support reproducing a specific build. To do this,
determine the build ids from the downgrade and upgrade builds, then run:

```
% $(fx get-build-dir)/host_x64/system_tests_upgrade \
  --ssh-private-key ~/.ssh/fuchsia_ed25519 \
  --downgrade-build-id 123456789... \
  --upgrade-build-id 987654321...
```

Or you can combine these options:

```
% $(fx get-build-dir)/host_x64/system_tests_upgrade \
  --ssh-private-key ~/.ssh/fuchsia_ed25519 \
  --downgrade-build-id 123456789... \
  --upgrade-fuchsia-build-dir $FUCHSIA_BUILD_DIR
```

There are more options to the test, to see them all run
`$(fx get-build-dir)/host_x64/system_tests_upgrade -- -h`.

### Reboot Testing

The system tests support running reboot tests, where a device is rebooted a
configurable number of times, or errs out if a problem occurs. This
can be done by running:

```
% $(fx get-build-dir)/host_x64/system_tests_reboot \
  --ssh-private-key ~/.ssh/fuchsia_ed25519 \
  --fuchsia-build-dir $FUCHSIA_BUILD_DIR
```

Or if you want to test a build, you can use:

* `--builder-name fuchsia/global.ci/fuchsia-x64-release-build_only`, to test the
  latest build published by that builder.
* `--build-id 1234...` to test the specific build.

### Tracking Testing

The system tests support running tracking tests, where a device is
continuously updated to the latest available version, or errs out if a problem
occurs. This can be done by running:

```
% $(fx get-build-dir)/host_x64/system_tests_tracking \
  --ssh-private-key ~/.ssh/fuchsia_ed25519 \
  --downgrade-builder-name fuchsia/global.ci/fuchsia-x64-release-build_only \
  --upgrade-builder-name fuchsia/global.ci/fuchsia-x64-release-build_only
```

The `--downgrade-build*` argument is optional, and only necessary if you want to
start the tracking test from a known zero state.

Note that at the moment the only supported upgrade mode is
`--upgrade-builder-name $BUILDER_NAME`.

## Running the Tests

When running the system tests, it's helpful to capture the serial logs, and
system logs, and the test output to a file in order to triage any failures. This
is especially handy when cycle testing. To simplify the setup, the system-tests
come with a helper script `run-test` that can setup a `tmux` session
for you. You can run it like this:

```
% ${FUCHSIA_DIR}/src/sys/pkg/tests/system-tests/bin/run-test \
  -o ~/logs \
  --tty /dev/ttyUSB0 \
  $(fx get-build-dir)/host_x64/system_tests_upgrade \
  --downgrade-builder-name fuchsia/global.ci/fuchsia-x64-release-build_only \
  --upgrade-fuchsia-build-dir $(fx get-build-dir)
```

This will setup a `tmux` with 3 windows, one for the serial session on
`/dev/ttyUSB0`, one for the system logs, and one for the test. All output from
the `tmux` windows will be saved into `~/logs`.

See the `run-test --help` for more options.

## Running the tests locally in the Fuchsia Emulator (experimental)

At the moment, the builtin `fx emu` does not bring up a configuration that can be
OTA-ed. Until this is implemented, the `bin/` directory contains some helper
scripts that bring up an OTA-able Fuchsia emulator. Follow these instructions to
it.

The `run-emu` script will create a Fuchsia EFI image, launch QEMU, then run the
paver. To create the image the script wraps `fx make-fuchsia-vol`. To launch
QEMU there is a modified version of `run-zircon` (also in the `bin/` directory)
that uses OVMF to allow us to run the bootloader. Any arguments you pass to the
script will be passed through to QEMU.

One thing to note, by default, you won't see any terminal output in the emulator
after an OTA. To restore this behavior, you need an extra build argument to send
output to the terminal:

```
% fx set ... \
  --with //src/sys/pkg:tests \
  --args 'kernel_cmdline_args=["kernel.serial=legacy"]'
% fx build
```

With all that setup, you now should be able to run the tests. First, launch the
emulator:

```
% ./bin/run-emu
```

In a new terminal, run the tests. For example, to run the upgrade tests:

```
% $(fx get-build-dir)/host_x64/system_tests_upgrade \
  --ssh-private-key ~/.ssh/fuchsia_ed25519 \
  --downgrade-builder-name fuchsia/global.ci/fuchsia-x64-release-build_only \
  --upgrade-fuchsia-build-dir $(fx get-build-dir) \
  --device step-atom-yard-juicy
```

Note that your QEMU device will always be named `step-atom-yard-juicy`.
Explicitly naming the device for the test will help prevent accidental upgrades
to your other devices.

[OVMF]: https://github.com/tianocore/tianocore.github.io/wiki/OVMF
