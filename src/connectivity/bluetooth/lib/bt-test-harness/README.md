# bt-test-harness

Implementations of the `TestHarness` trait for common Bluetooth FIDL interfaces. These are useful for writing integration tests against the interfaces.

See the [test-harness crate](/src/connectivity/bluetooth/lib/test-harness) for a description of the `TestHarness` trait.

» The typical structure of a harness file contains the following parts:
For each harness, we typically include the following:
  1. A definition of the state needed for the harness.
  2. An implemention of the TestHarness trait to define initialization and termination for the harness for each test case
  3. Additional utility functions specific to this helper - commonly predicate functions for simpler asynchronous expectations for verifying the harness state under test (see [fuchsia_bluetooth::expectation](/src/connectivity/bluetooth/lib/fuchsia-bluetooth/expectation.rs)
