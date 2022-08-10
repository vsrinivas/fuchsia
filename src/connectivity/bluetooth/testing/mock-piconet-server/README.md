# Mock Piconet Server

The Mock Piconet Server (MPS) component is used in integration tests for the Bluetooth
[profiles](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/connectivity/bluetooth/profiles/).

The server manages a fake piconet of peers, and simulates the behavior of the
[Profile Server](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/connectivity/bluetooth/core/bt-host/fidl/profile_server.h). The component implements the [ProfileTest](https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.bluetooth.bredr/profile_test.fidl) protocol, which can be used to
register mock peers in the piconet, launch profiles to test, and drive peer behavior. The server supports establishing L2CAP connections.
For RFCOMM connections, test writers should use the [RFCOMM component](../../profiles/bt-rfcomm) as an intermediary.

## Build Configuration

To include the mock piconet server and its tests in your build, add:

`--with //src/connectivity/bluetooth/testing/mock-piconet-server` and
`--with //src/connectivity/bluetooth/testing/mock-piconet-server:tests`
to your `fx set`.


To run the unit tests for the server: `fx test mock-piconet-server-tests`.

The Mock Piconet Server is referenced using a [relative URL](https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0104_relative_urls).
Make sure to include the MPS component in the `deps` of your integration test package.

## Library

The Mock Piconet Server provides a client-facing library of utilities to launch and interact
with the server.
To use the tools provided in the library, add `//src/connectivity/bluetooth/testing/mock-piconet-server:lib` to the
`BUILD.gn` of your test component.

## Examples

Import the `PiconetHarness` to your test to get started. Define the topology by adding profiles-under-test or mock
piconet members to be driven by the test code. Use the [client library](src/lib_v2.rs) to route capabilities needed & exposed by the
test.
Check out the [AVRCP Integration Tests](../../profiles/tests/bt-avrcp-integration-tests/src/main.rs)
for an example.
