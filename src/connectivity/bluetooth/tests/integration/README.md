# Bluetooth Integration Tests

We currently have tests that exercise the Host driver directly via the bluetooth.host interface, as well as tests that exercise the le.central and bluetooth.control interfaces, mediated via bt-gap. There are only a small number of tests so far, but we hope to extend these in the coming weeks and months. The tests use the fake hci driver as the back-end, which means they run in QEMU and the full suite should take a couple of seconds to run on a workstation.

## Run the Tests

First, include the bluetooth tests in your package configuration:

```
  $ fx set [...] \
    --available=garnet/packages/tests/bluetooth,garnet/packages/testing/run_test_component
```

NOTE: Before you run the tests, ensure that bt-gap is **not** running. Due to a limit with our current sandboxing, the tests will not execute correctly if a system bt-gap is running as it will bind to our test hosts. See fxb/BT-802.

You can run the tests from your development host via fx:

```
  $  fx run-test bluetooth-tests -t bt-integration-tests
```
Or directly on the Target machine:

```
  $ runtests -t bt-integration-tests
```

## Writing Tests

The current tests should serve as good examples for adding new tests, or new harnesses for other endpoints or fidl interfaces. We leverage the `expectation` module in the `fuchsia-bluetooth` library to provide a clear and concise API for defining asynchronous expectations. See [fuchsia-bluetooth::expectation](../../lib/fuchsia-bluetooth/src/expectation.rs)

Tests are run from [main.rs](src/main.rs) via the `run_test!` macro and must be functions from some type `H: TestHarness` and return some type `F: Future<Result(), failure::Error>`, where returning an `Error` indicates the test failed for the given reason. The `TestHarness` trait defines how to initialize the harness - usually setting up a fidl proxy of some kind - and how to execute the future to completion, normally via `run_singlethreaded`.

Test cases can be found in [src/tests](src/tests/), harnesses are defined in [src/harness](src/harness/).
