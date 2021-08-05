# emergency

This component provides an Emergency Location Service. The service passively
waits for Wifi scan results (via the
`fuchsia.location.sensor.WlanBaseStationWatcher` protocol), as well as Emergency
Location requests (via the `fuchsia.location.position.EmergencyProvider`
protocol). When an `EmergencyProvider` request arrives, this component uses the
Google Maps API to resolve cached scan results to the current position.

Note that access to the Google Maps API requires an API key. The key should be
provided via the
[config-data](https://fuchsia.dev/fuchsia-src/development/components/config_data)
mechanism.

## Building

To add this component to your build, append
`--with //src/connectivity/location/emergency`
to the `fx set` invocation.

## Running

```
$ fx ffx component run fuchsia-pkg://fuchsia.com/emergency#meta/emergency.cm
```

## Testing

Unit tests for emergency are available in the `emergency-tests`
package.

```
$ fx test emergency-tests
```
