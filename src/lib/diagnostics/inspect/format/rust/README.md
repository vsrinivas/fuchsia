# `inspect_format`

`inspect_format` is a library defining the VMO format for Inspect in Rust.
Docs are available [here](/docs/reference/diagnostics/inspect/vmo-format.md).

## Building

This project should be automatically included in builds.

## Using

`inspect_format` can be used by depending on the
`//src/lib/diagnostics/inspect/format/rust` GN target and then using
the `inspect_format` crate in a Rust project.

`inspect_format` is not available in the SDK.

## Testing

Unit tests for `inspect_format` are available in the
`inspect-format-tests` package:

```
$ fx test inspect-format-tests
```

You'll need to include `//src/lib/diagnostics/inspect/format/rust:tests` in your
build, either by using `fx args` to put it under `universe_package_labels`, or
by `fx set [....] --with //src/lib/diagnostics/inspect/format/rust:tests`.
