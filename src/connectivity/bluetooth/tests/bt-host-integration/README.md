# fuchsia.bluetooth.host/Host Integration Tests

Integration tests for the bt-host implementation of the fuchsia.bluetooth.host/Host FIDL protocol.

## Testing

Add the following to your Fuchsia set configuration to include the tests:

`--with //src/connectivity/bluetooth/tests/bt-host-integration-tests`

To run the tests:

```
fx test bt-host-integration-tests
```
