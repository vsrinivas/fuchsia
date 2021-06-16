# `inspect_fidl_load`

`inspect_fidl_load` is a library for finding and loading
fuchsia.inspect.deprecated.Inspect files.

## Building

This project should be automatically included in builds.

## Using

`fuchsia_inspect` can be used by depending on the
`//src/diagnostics/lib/inspect-fidl-load` gn target and then using
the `inspect_fidl_load` crate in a rust project.

`inspect_fidl_load` is not available in the sdk and is intended to be used only by
diagnostics binaries.

## Testing

unit tests for `inspect_fidl_load` are available in the
`inspect_fidl_load` package:

```
$ fx test inspect_fidl_load_tests
```

You'll need to include `//src/diagnostics/lib/inspect-fidl-load:tests` in your
build, either by using `fx args` to put it under `universe_package_labels`, or
by `fx set [....] --with //src/diagnostics/lib/inspect-fidl-load:tests`.
