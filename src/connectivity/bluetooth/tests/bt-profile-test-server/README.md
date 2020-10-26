# Profile Test Server

The Profile Test Server component is used in integration tests for the bluetooth
[profiles](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/connectivity/bluetooth/profiles/).

The server manages a fake piconet of peers, and simulates the behavior of the
[Profile Server](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/connectivity/bluetooth/core/bt-host/fidl/profile_server.h). The component implements the [ProfileTest](https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.bluetooth.bredr/profile_test.fidl) interface, which can be used to
register mock peers in the piconet, launch profiles to test, and drive peer behavior.

## Build Configuration

To include the profile server in your build, add:

`--with //src/connectivity/bluetooth/tests/bt-profile-test-server` and
`--with //src/connectivity/bluetooth/tests/bt-profile-test-server:bt-profile-test-server-tests`
to your `fx set`.


To run the unit tests for the server: `fx test bt-profile-test-server-tests`.
