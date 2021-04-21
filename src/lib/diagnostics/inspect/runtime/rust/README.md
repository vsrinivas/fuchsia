# `inspect_runtime`

`inspect_runtime` is a library that exposes Inspect data from a component to the
framework.

## Building

This project should be automatically included in builds.

## Using

`inspect_runtime` can be used by depending on the
`//src/lib/diagnostics/inspect/runtime/rust` GN target and then using
the `inspect_runtime` crate in a Rust project.

`inspect_runtime` is not available in the SDK.

## Testing

Unit tests for `inspect_runtime` are available in the
`inspect-runtime-tests` package:

```
$ fx test inspect-runtime-tests
```

You'll need to include `//src/lib/diagnostics/inspect/runtime/rust:tests` in your
build, either by using `fx args` to put it under `universe_package_labels`, or
by `fx set [....] --with //src/lib/diagnostics/inspect/runtime/rust:tests`.
