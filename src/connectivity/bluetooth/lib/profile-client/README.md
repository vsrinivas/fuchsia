# Bluetooth Library: Profile Client

Provides an easy way to to interact with the `fuchsia.bluetooth.bredr.Profile` protocol. Allows
clients to register service searches & advertisements and provides a Stream implementation to
receive `Profile` requests.

To use the library, include `//src/connectivity/bluetooth/lib/profile-client` in your BUILD.gn.
Typical usage is to instantiate a `ProfileClient` and register service advertisements & searches.

## Testing

Add the following to your Fuchsia set configuration to include the library unit tests:

`--with //src/connectivity/bluetooth/library/profile-client:tests`

To run the tests:

```
fx test profile-client-tests
```
