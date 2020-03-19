# Run an end-to-end test

This guide provides instructions on how to run an end-to-end test for testing a
Fuchsia product.

This guide uses [QEMU](https://www.qemu.org/){:.external} to emulate a device
that runs Fuchsia. As for the end-to-end test, the guide uses the
<code>[screen_is_not_black_test](/src/tests/end_to_end/screen_is_not_black/)</code>
end-to-end test.

The `screen_is_not_black_test` end-to-end test reboots a device under test
(DUT), waits 100 seconds, and takes a snapshot of the device’s screen. If the
snapshot image is not a black screen, the test concludes that Fuchsia is
successfully up and running on the device after reboot.

To run this example end-to-end test:

*   [Build a Fuchsia image to include the end-to-end test](#build-a-fuchsia-image-to-include-the-end-to-end-test)
*   [Start the emulator with the Fuchsia image](#start-the-emulator-with-the-fuchsia-image)
*   [Run the end-to-end test](#run-the-end-to-end-test)

## Prerequisites

Verify the following requirements:

*   Set up your Fuchsia development environment (see
    [Get Fuchsia source code](/docs/development/source_code/README.md)).
*   [Configure an IPv6 network](/docs/development/run/qemu.md#enabling_networking_under_qemu.md)
    for the emulator:

    ```sh
    sudo ip tuntap add dev qemu mode tap user $USER
    sudo ifconfig qemu up
    ```

## Build a Fuchsia image to include the end-to-end test {#build-a-fuchsia-image-to-include-the-end-to-end-test}

Before you can run the `screen_is_not_black_test` end-to-end test, you first
need to build your Fuchsia image to include the test in the build artifacts:

1.  To add the end-to-end test, run the `fx set` command with the following
    `--with` option:

    ```posix-terminal
    fx set workstation.qemu-x64 --with //src/tests/end_to_end/screen_is_not_black:test
    ```

    `//src/tests/end_to_end/screen_is_not_black` is a test directory in the
    Fuchsia source tree. The
    <code>[BUILD.gn](/src/tests/end_to_end/screen_is_not_black/BUILD.gn)</code>
    file in this directory defines the <code>test</code> target to include the
    <code>screen_is_not_black_test</code> end-to-end test in the build
    artifacts.

1.  Build your Fuchsia image:

    ```posix-terminal
    fx build
    ```

When the `fx build` command completes, the build artifacts now include the
`screen_is_not_black_test` end-to-end test, which you can run from your host
machine.

## Start the emulator with the Fuchsia image {#start-the-emulator-with-the-fuchsia-image}

Start the emulator with your newly built Fuchsia image and run a
[package repository server](/docs/development/build/fx.md#serve-a-build):

Note: The steps in this section assume that you don't have any terminals
currently running QEMU or the `fx serve` command.

1.  In a new terminal, start the emulator:

    ```posix-terminal
    fx emu -N
    ```

    The `fx emu` command starts the emulator with a Fuchsia image from your default build
    directory.

1.  Run the
    [`fx set-device`](/docs/development/build/fx.md#using-multiple-fuchsia-devices)
    command and select `step-atom-yard-juicy` (the emulator’s default device
    name) to be your device, for example:

    <pre>
    $ fx set-device

    Multiple devices found, please pick one from the list:
    1) rabid-snort-wired-tutu
    2) step-atom-yard-juicy
    #? <b>2</b>
    New default device: step-atom-yard-juicy
    </pre>

1.  In another new terminal, start a package server:

    ```posix-terminal
    fx serve
    ```

    The `fx serve` command continues to run as a package server for the device.

## Run the end-to-end test {#run-the-end-to-end-test}

Run the `screen_is_not_black_test` end-to-end test:

```posix-terminal
fx run-e2e-tests screen_is_not_black_test
```

When the test passes, this `fx` command prints the following output in the end:

```none
Saw a screen that is not black.

[FINE]: Running over ssh: killall sl4f.cmx
01:04 +1: All tests passed!


1 of 1 test passed
```

### Run any end-to-end test {#run-any-end-to-end-test}

Use the `fx run-e2e-tests` command to run an end-to-end test from your host
machine:

```posix-terminal
fx run-e2e-tests <TEST_NAME>
```

Some product configurations may include a set of end-to-end tests by default.
However, if you want to run an end-to-end test that is not part of your product
configuration, configure and build your Fuchsia image to include the specific
test:

```sh
fx set <PRODUCT>.<BOARD> --with <TEST_DIRECTORY:TARGET>
fx build
```

For example, the following command configures your build artifacts to include
all the end-to-end tests in the
<code>[//src/tests/end_to_end/perf](/src/tests/end_to_end/perf/)</code> test
directory:

```posix-terminal
fx set workstation.qemu-x64 --with //src/tests/end_to_end/perf:test
```

Note: Some end-to-end tests can only run on specific product configurations.

For the list of all available end-to-end tests in the Fuchsia repository, see
the [//src/tests/end\_to\_end](/src/tests/end_to_end/) directory.
