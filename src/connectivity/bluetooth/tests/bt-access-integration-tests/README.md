# fuchsia.bluetooth.sys.Access Integration Tests

Integration tests for the Sapphire implementation of the fuchsia.bluetooth.sys.Access FIDL protocol.

## Testing

Add the following to your Fuchsia set configuration to include the tests:

`--with //src/connectivity/bluetooth/tests/bt-access-integration-tests`

To run the tests:

```
fx test bt-access-integration-tests
```
