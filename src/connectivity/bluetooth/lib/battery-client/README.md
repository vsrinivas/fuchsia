# Bluetooth Library: Battery Client

A library for interacting with the Fuchsia Battery Manager.

## Build

Make sure to include the `client.shard.cml` in your component's manifest. This will route the
`fuchsia.power.BatteryManager` capability to your component.

## Testing

Add the following to your Fuchsia set configuration to include the library unit tests:

`--with //src/connectivity/bluetooth/library/battery-client:tests`

To run the tests:

```
fx test battery-client-tests
```
