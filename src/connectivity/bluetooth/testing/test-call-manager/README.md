# test-call-manager

This library provides an implementation of the client side of the fuchsia.bluetooth.hfp FIDL APIs.
It is designed to be used from within tests. It is _not_ designed to build a production quality call
manager.

The entry point to the library is the `TestCallManager` which can be used to interact with a
bt-hfp-audio-gateway instance. The `TestCallManager` state is set through methods on the manager.
Using background tasks, it asynchronously handles requests and responses made by the
bt-hfp-audio-gateway. It does this using the state that has been set by the owner of the
`TestCallManager`.

## Build

This library relies on the `fuchsia.bluetooth.hfp.Hfp` and `fuchsia.bluetooth.hfp.test.HfpTest`
capabilities. Ensure both are available by including them in the integration test manifest.

## Test

This library is used to build integration tests. Its functionality is exercised in those tests
directly.
