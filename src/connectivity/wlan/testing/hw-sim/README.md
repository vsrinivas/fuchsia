## Introduction

WLAN Hardware Simulator, or `hw-sim`, is a framework used to simulate a Fuchsia compatible WLAN device, its corresponding vendor driver and the radio environment it is in. It is mostly used to perform integration tests that involve some interaction between the system and the components mentioned above.

## Run tests locally

The most convenient way to run this test locally is to run it in a QEMU instance.

##### Emulator without network (run test from the emulator command prompt directly)

1. Make sure `src/connectivity/wlan:tests` is included in `with-base` so that the driver for the fake wlantap device is loaded in QEMU. For example:

    ```
    fx set core.x64 --with-base src/connectivity/wlan:tests,bundles:tools
    ```

1. Start the emulator instance with

    ```
    fx emu
    ```

    Note: If QEMU is not working, troubleshoot steps can be found [here](/docs/getting_started#boot_from_qemu)

1. In the QEMU command prompt, run the tests individually in the `test` directory. For example:

    ```
    run-test-component simulate_scan
    ```

1. Rust tests hides `stdout` when tests pass. To force displaying `stdout`, run the test with `--nocapture`

    ````
    run-test-component simulate_scan --nocapture
    ````

1. After the test finishes, exit QEMU by

    ```
    dm shutdown
    ```

##### Emulator with network (run test with `fx emu` from host)
1. Setup QEMU network by following [these instructions](/docs/getting_started#enabling_network)

    (*Googlers*: Search "QEMU network setup" for additional steps for your workstation)

1. Same `fx set` as the previous option
1. Start the emulator with `-N` to enable network support

    ```
    fx emu -N
    ```

1. From another terminal on the **host**,

    ```
    fx run-test wlan-hw-sim-test
    ```

1. To force `stdout` display, pass `--nocapture` with additional `--` so that it does not get parsed by `run-test`
    ````
    fx run-test wlan-hw-sim-test -- --nocapture
    ````

1. Individual tests can be run with

    ```
    fx run-test wlan-hw-sim-test -t simulate_scan
    ```

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
     fx set core.x64 --with-base bundles:tools,src/connectivity/wlan:tests,bundles:tools
     ````
1. Start an emulator instance
    ````
    fx emu -N
    ````

1. Run the test in the emulator command prompt (not with `fx emu`)
     ```
     for i in $(seq 1 1000); do run-test-component simulate_scan || break; echo success: attempt $i; done;
     ```
