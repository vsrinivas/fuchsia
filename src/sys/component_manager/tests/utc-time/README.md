# utc-time-tests

This test verifies that `component_manager`, when launched with the `--maintain-utc-time` flag,
provides a `fuchsia.time.Maintenance` FIDL capability that can be routed to a
component. It further verifies that the same clock is vended to consumers of
UTC time.

This test launches a `realm` component, which has `maintainer` and
`time_client` child components. The `maintainer` uses
`fuchsia.time.Maintenance` to update the time on a clock, which is observed by
`time_client`.

## Building

To add this test to your build, append
`--with src/sys/component_manager/tests/utc-time`
to the `fx set` invocation.

## Testing

```
$ fx test utc-time-tests
```

