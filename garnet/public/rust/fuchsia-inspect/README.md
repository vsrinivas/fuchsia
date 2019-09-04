# `fuchsia_inspect`

`fuchsia_inspect` is a library for writing and reading Inspect-formatted
VMOs. Docs are available [here](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/docs/development/inspect/vmo-format/README.md).

## Building

This project should be automatically included in builds.

## Using

`fuchsia_inspect` can be used by depending on the
`//garnet/public/rust/fuchsia-inspect` GN target and then using
the `fuchsia_inspect` crate in a Rust project.

`fuchsia_inspect` is not available in the SDK.

## Testing

Unit tests for `fuchsia_inspect` are available in the
`fuchsia_inspect_lib_test` package:

```
$ fx run-tests fuchsia_inspect_lib_test
```

You'll need to include `//garnet/public/rust/fuchsia-inspect:tests` in your
build, either by using `fx args` to put it under `universe_package_labels`, or
by `fx set [....] --with //garnet/public/rust/fuchsia-inspect:tests`.
