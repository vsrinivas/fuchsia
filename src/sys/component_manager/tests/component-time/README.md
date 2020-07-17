# component-time

A simple component unit-test that tests the current UTC time, in both component v1
and component v2 frameworks.

## Building

To add this component to your build, append
`--with src/sys/component_manager/tests/component-time`
to the `fx set` invocation.

## Testing

Unit tests for component-time are available in the `component-v1-time-unittests`
and `component-v2-time-unittests` packages.

```
$ fx test component-v1-time-unittests
$ fx test component-v2-time-unittests
```
