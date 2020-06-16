# utc-time-tests

This test verifies that `component_manager`, when launched with the `--maintain-utc-time` flag,
provides a `fuchsia.time.Maintenance` FIDL capability that can be routed to a component.

## Building

To add this test to your build, append
`--with src/sys/component_manager/tests/utc-time`
to the `fx set` invocation.

## Testing

```
$ fx test utc-time-tests
```

