# `inspect_formatter`

`inspect_formatter` is a library for formatting inspect node hierarchies in
JSON and text.

## Building

This project should be automatically included in builds.

## Using

`fuchsia_inspect` can be used by depending on the
`//src/diagnostics/lib/inspect-formatter` gn target and then using
the `inspect_formatter` crate in a rust project.

`inspect_formatter` is not available in the sdk and is intended to be used only by
diagnostics binaries.

## Testing

unit tests for `inspect_formatter` are available in the
`inspect_formatter` package:

```
$ fx run-test inspect_formatter_tests
```

You'll need to include `//src/diagnostics/lib/inspect-formatter:tests` in your
build, either by using `fx args` to put it under `universe_package_labels`, or
by `fx set [....] --with //src/diagnostics/lib/inspect-formatter:tests`.
