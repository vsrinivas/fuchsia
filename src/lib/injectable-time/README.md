# `injectable-time`

`injectable-time` is a library to support dependency-injecting a time source.

It provides a trait `TimeSource` with one function, `now()`.

It provides two structs, `UtcTime` and `FakeTime`, which implement
`TimeSource`. `FakeTime` has a function `set` which can be used in tests.

The `now()` function of `UtcTime` returns the number of nanoseconds since
the Unix epoch. The library uses i64 instead of an os-dependent time type so
it can run host-side and in Fuchsia.

Any struct that needs an injectable time source can store a
`&'a dyn TimeSource`. Note the lack of `mut`.

See the unit tests in injectable_time.rs for a usage example.

## Building

This project should be automatically included in builds.

## Using

`injectable-time` can be used by depending on the
`//src/diagnostics/lib/util/injectable-time` gn target and then using
the `injectable-time` crate in a rust project.

`injectable-time` is not available in the sdk and is intended to be used only by
diagnostics binaries.

## Testing

Unit tests for `injectable-time` are available in the
`injectable-time` package:

```
$ fx test injectable_time_lib_test
```

You'll need to include `//src/diagnostics/lib/injectable-time:tests` in your
build, either by using `fx args` to put it under `universe_package_labels`, or
by `fx set [....] --with //src/diagnostics/lib/injectable-time:tests`.
format
