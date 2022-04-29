# Bluetooth Component: Peripheral Hub

## Build Configuration

Ensure `//src/connectivity/bluetooth/profiles/bt-peripheral-hub` is in your Fuchsia build. To
include it in the universe set of packages, use the `fx set` configuration or `fx args`. To include
it in the base or cached set of packages, update the product-specific `.gni` file.

## Testing

Add the following to your Fuchsia configuration to include the component unit tests in your build:

`//src/connectivity/bluetooth/profiles/bt-peripheral-hub:bt-peripheral-hub-tests`

To run the tests:

```
fx test bt-peripheral-hub-tests
```
