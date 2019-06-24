## Introduction

WLAN Hardware Simulator, or `hw-sim`, is a framework used to simulate a Fuchsia compatible WLAN device, its corresponding vendor driver and the radio environment it is in. It is mostly used to perform integration tests that involve some interaction between the system and the components mentioned above. It can also be started from Fuchsia command line to function as a fake WLAN device and its radio environment.

## Run `wlan-hw-sim-tests` locally

At the time of writing, this test will **NOT** pass on any platform with a supported physical WLAN device. Unplug any WLAN dongle if you intend to run this test on a physical device. Do **NOT** attempt to run the test on devices with supported built-in WLAN adapter, such as a device with an ath10k device, unless the adapter's driver is excluded in the build.

The most convenient way to run this test locally is to run it in a QEMU instance.

##### QEMU without network (run test from QEMU command prompt directly)

1. Make sure QEMU is working by following https://fuchsia.googlesource.com/fuchsia/+/master/docs/getting_started.md#Boot-from-QEMU
1. Make sure `src/connectivity/wlan:tests` is included in `with-base` so that the driver for the fake wlantap device is loaded in QEMU. For example:

    ```
    fx set core.x64 --with-base src/connectivity/wlan:tests
    ```

1. Start the QEMU instance with

    ```
    fx run -k
    ```

1. In the QEMU command prompt, run the test

    ```
    run wlan-hw-sim-tests
    ```

1. Rust tests hides `stdout` when tests pass. To force displaying `stdout`, run the test with `--nocapture`

    ````
    run wlan-hw-sim-tests --nocapture
    ````

1. After the test finishes, exit QEMU by

    ```
    dm shutdown
    ```

##### QEMU with network (run test with `fx run` from host)
1. Setup QEMU network by following https://fuchsia.googlesource.com/fuchsia/+/master/docs/getting_started.md#enabling-network

    (*Googlers*: Search "QEMU network setup" for additional steps for your workstation)

1. Same `fx set` as the previous option
1. Start QEMU with `-N` to enable network support

    ```
    fx run -kN
    ```

1. From another terminal on the **host**,

    ```
    fx run-test wlan-hw-sim-tests
    ```

1. To force `stdout` display, pass `--nocapture` with additional `--` so that it does not get parsed by `run-test`
    ````
    fx run-test wlan-hw-sim-tests -- --nocapture
    ````

## Special notes for debugging flakiness in CQ

##### Enable nested KVM to mimic the behavior of CQ bots

1. Run these commands to enable nested KVM for intel CPU to mimic the behavior of CQ bots. They to be run every time your workstation reboots.

    ```
    sudo modprobe -r kvm_intel
    sudo modprobe kvm_intel nested=1
    ```

1. Verify the result by checking the content of the following file is `Y` instead of `N`.

    ```
    $ cat /sys/module/kvm_intel/parameters/nested
    Y
    ```

##### Run the test repeatedly

Often when debugging flakiness, it is more helpful to run the tests repeatedly. Here is how:

1. Include `bundles:tools` in your build, so that the `seq` command is available. The previous example is now
     ````
     fx set core.x64 --with-base bundles:tools,src/connectivity/wlan:tests
     ````
1. Start a QEMU instance
    ````
    fx run -k
    ````

1. Run the test in QEMU command prompt (not with `fx run`)
     ````
     for i in $(seq 1 1000); do run wlan-hw-sim-tests || break; echo success: attempt $i; done;
     ````

## Running `hw-sim` in the command line

WLAN Hardware Simulator can be started from the command line. It currently supports `scan` and `connect`. It is recommended to run it on a physical device, since it has to be running all the time while `wlan` commands are issued from another terminal window. This can also be achieved in QEMU (with network support) by starting the package from within QEMU and issuing the commands via `fx shell`

1. Start `hw-sim` in one terminal:
    ```
    run fuchsia-pkg://fuchsia.com/wlan-hw-sim#meta/wlan-hw-sim.cmx
    ```

    Note: Full URL must be used because the package has the same prefix as package `wlan-hw-sim-tests`

1. From another terminal:
    ````
    wlan scan
    wlan connect fakenet
    ````

    Note: In the simulated radio envrionment, there is only one network with SSID `fakenet` and no protection.
