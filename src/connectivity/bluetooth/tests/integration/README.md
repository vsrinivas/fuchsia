# Bluetooth Integration Tests

This module defines integration tests for the Fuchsia Bluetooth stack. Tests are written in Rust and run as Fuchsia components. Components under test are imported into the test component sandbox and exercised via FIDL.

Functionality covered includes:
  * bt-host driver (directly via fuchsia.bluetooth.host)
  * Low energy (via fuchsia.bluetooth.le)
  * GAP (bt-gap) (via fuchsia.bluetooth.control)

## Requirements
The tests use the HCI driver emulator, so no hardware is required, and they can be run on qemu (e.g. via `fx qemu`).

## Known issues
**The tests cannot run correctly if `bt-gap` is already running on the system, as drivers are not sandboxed sufficiently. The existing bt-gap may claim the host.fidl channel from the bt-host, which will prevent the emulated drivers behaving correctly for our tests.** *see fxb/1395*

## Build the tests
To ensure the tests build correctly, build the integration test package:
```
   fx build src/connectivity/bluetooth/tests/integration
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
Run from your development host via the `fx` tool:
```
  $  fx run-test bluetooth-tests -t bt-integration-tests
```

Run directly in a shell on the fuchsia target:
```
  $ runtests -t bt-integration-tests
```

## Writing Tests
The current tests should serve as good examples for adding new tests, or new harnesses for other endpoints or fidl interfaces. We leverage the `expectation` module in the `fuchsia-bluetooth` library to provide a clear and concise API for defining asynchronous expectations. See [fuchsia-bluetooth::expectation](../../lib/fuchsia-bluetooth/src/expectation.rs)

Tests are run from [main.rs](src/main.rs) via the `run_test!` macro and must be functions from some type `H: TestHarness` and return some type `F: Future<Result(), failure::Error>`, where returning an `Error` indicates the test failed for the given reason. The `TestHarness` trait defines how to initialize the harness - usually setting up a fidl proxy of some kind - and how to execute the future to completion, normally via `run_singlethreaded`.

Test cases can be found in [src/tests](src/tests/), harnesses are defined in [src/harness](src/harness/).
