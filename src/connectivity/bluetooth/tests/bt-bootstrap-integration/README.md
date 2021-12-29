# fuchsia.bluetooth.sys/Bootstrap Integration Tests

Integration tests for the Sapphire implementation of the fuchsia.bluetooth.sys/Bootstrap FIDL protocol.

## Testing

Add the following to your Fuchsia set configuration to include the tests:

`--with //src/connectivity/bluetooth/tests/bt-bootstrap-integration-tests`

To run the tests:

```
fx test bt-bootstrap-integration-tests
```
