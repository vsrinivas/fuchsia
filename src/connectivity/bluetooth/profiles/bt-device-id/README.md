# Bluetooth Profile: DI

This component implements the Device Identification (DI) Profile as specified by the Bluetooth SIG.

## Build Configuration

Add the following to your Fuchsia set configuration to include the component:

`--with //src/connectivity/bluetooth/profiles/bt-device-id`

## Testing

Add the following to your Fuchsia set configuration to include the component unit tests:

`--with //src/connectivity/bluetooth/profiles/bt-device-id:bt-device-id-tests`

To run the tests:

```
fx test bt-device-id-tests
```
