# `selectors`

`selectors` is a library for parsing and converting diagnostics selectors.

## Building

This project should be automatically included in builds.

## Using

`selectors` can be used by depending on the
`//src/diagnostics/lib/selectors` gn target and then using
the `selectors` crate in a rust project.

`selectors` is not available in the sdk and is intended to be used only by
diagnostics binaries.

## Testing

Unit tests for `selectors` are available in the
`selectors` package:

```
$ fx run-test selectors_tests
```

You'll need to include `//src/diagnostics/lib/selectors:tests` in your
build, either by using `fx args` to put it under `universe_package_labels`, or
by `fx set [....] --with //src/diagnostics/lib/selectors:tests`.
