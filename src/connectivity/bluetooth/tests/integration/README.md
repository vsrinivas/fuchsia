# Bluetooth Integration Tests

This module defines integration tests for the Fuchsia Bluetooth stack. Tests are written in Rust and run as Fuchsia components. Components under test are imported into the test component sandbox and exercised via FIDL.

Functionality covered includes:
  * bt-host driver (directly via fuchsia.bluetooth.host)
  * Low energy (via fuchsia.bluetooth.le)
  * GAP (bt-gap) (via fuchsia.bluetooth.control)

## Requirements
The tests use the HCI driver emulator, so no hardware is required, and they can be run on qemu (e.g. via `fx qemu`).

## Known issues
**The tests cannot run correctly if `bt-gap` is already running on the system, as drivers are not sandboxed sufficiently. The existing bt-gap may claim the host.fidl channel from the bt-host, which will prevent the emulated drivers behaving correctly for our tests.** *see fxbug.dev/1395*

## Build the tests
To ensure the tests build correctly, build the integration test package:
```
   $ fx build src/connectivity/bluetooth/tests/integration
```

In order to run the tests:
* Include the Bluetooth system in your build configuration.
* Include these tests in your package universe.
* Include the hci-emulator in the base-image. *The *hci-emulator* **must** be in the **base-image** for the build, as drivers cannot currently be loaded from the package server.*

```
  $ fx set [..] \
    --with-base=src/connectivity/bluetooth/hci/emulator
    --with=src/connectivity/bluetooth
    --with=src/connectivity/bluetooth/tests

  $ fx build
```

## Run the Tests

*Note: you must re-pave your device if it does not already contain the hci emulator. The tests use the hci emulator driver, which must be in the base-image and cannot be served from the package server.*

Run from your development host via the `fx` tool:
```
  $  fx run-test bluetooth-tests -t bt-integration-tests
```

Run directly in a shell on the fuchsia target:
```
  $ runtests --names bt-integration-tests
```

### Run on QEMU

The easiest way to run the tests is in QEMU. This lets you run the tests with no additional hardware, and set up a system quickly with the correct image. On an x64 workstation, you can run QEMU using the kvm back-end (via the `-k` switch), which will run at almost native speed of the host machine.

First, build the tests - using the x64 platform will allow you to use KVM for improved performance.

```
   $ fx set core.x64 \
     --with-base=src/connectivity/bluetooth/hci/emulator
     --with=src/connectivity/bluetooth
     --with=src/connectivity/bluetooth/tests

   $ fx build
```

In one terminal, run Fuchsia on QEMU
```
   // Start QEMU (-N to enable networking, -k to use KVM to emulate at near-native spead, though only works for x64 targets)
   $ fx qemu -kN
```

In another terminal, set your fx configuration to use the newly appeared QEMU device:

```
   $ fx set-device
```
*(If you have other fuchsia targets connected, you'll need to specify which one)*

Then run the package server:
```
   $ fx serve
```

Then finally in a third terminal, run the tests:
```
   $ fx run-test bluetooth-tests -t bt-integration-tests
```

## Writing Tests
The current tests should serve as good examples for adding new tests, or new harnesses for other endpoints or fidl interfaces. We leverage the `expectation` module in the `fuchsia-bluetooth` library to provide a clear and concise API for defining asynchronous expectations. See [fuchsia-bluetooth::expectation](../../lib/fuchsia-bluetooth/src/expectation.rs)

Tests are run from [main.rs](src/main.rs) via the `run_test!` macro and must be functions from some type `H: TestHarness` and return some type `F: Future<Result(), failure::Error>`, where returning an `Error` indicates the test failed for the given reason. The `TestHarness` trait defines how to initialize the harness - usually setting up a fidl proxy of some kind - and how to execute the future to completion, normally via `run_singlethreaded`.

Test cases can be found in [src/tests](src/tests/), harnesses are defined in [src/harness](src/harness/).
