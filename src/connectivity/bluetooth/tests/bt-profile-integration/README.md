# fuchsia.bluetooth.bredr/Profile Integration Tests

Integration tests for the Sapphire bt-host implementation of the
fuchsia.bluetooth.bredr/Profile FIDL protocol.

## Testing

Add the following to your Fuchsia set configuration to include the tests:

`--with //src/connectivity/bluetooth/tests/bt-profile-integration-tests`

To run the tests:

```
fx test bt-profile-integration-tests
```
