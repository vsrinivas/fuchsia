# Profile Test Server

The Profile Test Server component is used in integration tests for the bluetooth
[profiles](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/connectivity/bluetooth/profiles/).

The server manages a fake piconet of peers, and simulates the behavior of the
[Profile Server](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/connectivity/bluetooth/core/bt-host/fidl/profile_server.h). The component implements the [ProfileTest](https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.bluetooth.bredr/profile_test.fidl) interface, which can be used to
register mock peers in the piconet, launch profiles to test, and drive peer behavior. The server supports establishing L2CAP and RFCOMM connections.

## Build Configuration

To include the profile server and its tests in your build, add:

`--with //src/connectivity/bluetooth/testing/bt-profile-test-server` and
`--with //src/connectivity/bluetooth/testing/bt-profile-test-server:tests`
to your `fx set`.


To run the unit tests for the server: `fx test bt-profile-test-server-tests`.

## Library

The Profile Test Server also provides a client-facing library of utilities to launch and interact
with the server.
To use the tools provided in the library, add `//src/connectivity/bluetooth/testing/bt-profile-test-server:lib` to the
`BUILD.gn` of your test component.

Import the `ProfileTestHarness` to your test to get started! Instantiate the ProfileTestHarness and register mock peers using
the `ProfileTestHarness::register_peer` method. You can then emulate peer behavior by either operating directly on the mock peer or
launching a Bluetooth profile.