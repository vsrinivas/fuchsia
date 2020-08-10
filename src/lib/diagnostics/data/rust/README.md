# `diagnostics-data`

`diagnostics_data` is a library containing types for diagnostics data consumed through the
Archivist.

## Building

This project should be automatically included in builds.

## Using

`diagnostics-data` can be used by depending on the
`//src/lib/diagnostics/data/rust` gn target and then using the `diagnostics_data` crate
in a rust project.

## Testing

Unit tests for `diagnostics-data` are available in the `diagnostics-data-tests` package:

```
$ fx run-test diagnostics-data-tests
```

You'll need to include `//src/lib/diagnostics/data:tests` in your
build, either by using `fx args` to put it under `universe_package_labels`, or
by `fx set [....] --with //src/lib/diagnostics/data:tests`.
