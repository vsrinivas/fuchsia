# rust-logs-example

A simple example component which prints "hello, world!" to syslog and exits.

Includes an integration test which asserts on the contents of those logs.

## Building

To add this component to your build, append `--with examples/diagnostics/rust` to the
`fx set` invocation.

## Testing

Tests for this example are available in the `rust_logs_example_tests` package:

```
$ fx test rust_logs_example_tests
```
