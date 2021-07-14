# Mock Piconet Server

The Mock Piconet Server component is used in integration tests for the Bluetooth
[profiles](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/connectivity/bluetooth/profiles/).

The server manages a fake piconet of peers, and simulates the behavior of the
[Profile Server](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/connectivity/bluetooth/core/bt-host/fidl/profile_server.h). The component implements the [ProfileTest](https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.bluetooth.bredr/profile_test.fidl) protocol, which can be used to
register mock peers in the piconet, launch profiles to test, and drive peer behavior. The server supports establishing L2CAP connections.
For RFCOMM connections, test writers should use the [RFCOMM component](../../profiles/bt-rfcomm) as an intermediary.

The Mock Piconet Server currently supports integration tests written in both CFv1 and CFv2 frameworks.

## Build Configuration

To include the mock piconet server and its tests in your build, add:

`--with //src/connectivity/bluetooth/testing/mock-piconet-server` and
`--with //src/connectivity/bluetooth/testing/mock-piconet-server:tests`
to your `fx set`.


To run the unit tests for the server: `fx test mock-piconet-server-tests`.

## Library

The Mock Piconet Server provides a client-facing library of utilities to launch and interact
with the server.
To use the tools provided in the library, add `//src/connectivity/bluetooth/testing/mock-piconet-server:lib` to the
`BUILD.gn` of your test component.

## Examples

For CFv1, import the `ProfileTestHarness` to your test to get started. Instantiate the ProfileTestHarness and register mock peers using
the `ProfileTestHarness::register_peer` method. You can then emulate peer behavior by either operating directly on the mock peer or
launching a Bluetooth profile. For an example using the v1 framework, check out the [A2DP Source Integration Tests](../../profiles/tests/bt-a2dp-source-integration-tests/src/main.rs).

For CFv2, import the `PiconetHarness` to your test to get started. Define the topology by adding profiles-under-test or mock
piconet members to be driven by the test code. Use the [CFv2 Library](src/lib_v2.rs) to route capabilities needed & exposed by the
test.
For an example using the v2 framework, check out the [AVRCP Integration Tests](../../profiles/tests/bt-avrcp-integration-tests/src/main.rs).
