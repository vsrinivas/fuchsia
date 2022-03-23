# syslog

Syslog provides logging infrastructure and corresponding tests for Fuchsia.

## Building tests

Append `--with //sdk/lib/syslog/cpp:tests` to your `fx set` invocation.

## Running tests

To run host tests, use
```
$ fx test --host logging_cpp_unittests
```

Note that host tests are not currently supported on ARM MacOS, but should
function on Intel-based Macs or on certain Linux distributions.
