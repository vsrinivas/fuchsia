# rust-logs-example

A simple example component which prints "hello, world!" to syslog and exits.

Includes an integration test which asserts on the contents of those logs.

## Building

To add this component to your build, append `--with examples/diagnostics/rust` to the
`fx set` invocation.

## Running

```
$ fx shell run fuchsia-pkg://fuchsia.com/rust-logs-example#meta/rust-logs-example.cmx
```

## Testing

Tests for this example are available in the `rust-logs-example-tests` package:

```
$ fx test rust-logs-example-tests
```
