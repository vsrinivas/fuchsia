# Profile Test Server Library

The Profile Test Server Library provides functionality to help write integration tests
for Fuchsia Bluetooth [profiles](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/connectivity/bluetooth/profiles/).

The library provides tools for registering a peer in the piconet, registering a client for direct peer
manipulation, and various other convenience methods to simplify writing integration tests.

The library uses the [ProfileTest](https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.bluetooth.bredr/profile_test.fidl)
API. It is expected that integration tests will leverage the [Profile Test Server](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/connectivity/bluetooth/tests/bt-profile-test-server/), which manages a fake piconet of peers.

# Build Configuration

To use the tools provided in the library, add `//src/connectivity/bluetooth/lib/bt-profile-test-server` to
the `BUILD.gn` of your test component.

Import the `ProfileTestHarness` and `MockPeer` objects in your test to get started!
Common usage is to instantiate the `ProfileTestHarness` followed by registering mock peers using `register_peer`.
You can then drive peer behavior by either operating directly on the mock peer or launching a Bluetooth profile.