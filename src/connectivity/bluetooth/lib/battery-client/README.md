# Bluetooth Library: Battery Client

A library for interacting with the Fuchsia Battery Manager.

## Build

This library depends on the `fuchsia.power.battery.BatteryManager` capability. Make sure it is
routed to your component.

## Testing

Add the following to your Fuchsia set configuration to include the library unit tests:

`--with //src/connectivity/bluetooth/library/battery-client:tests`

To run the tests:

```
fx test battery-client-tests
```
