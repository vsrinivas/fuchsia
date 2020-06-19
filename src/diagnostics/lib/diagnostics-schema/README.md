# `diagnostics-schema`

`diagnostics_schema` is a library containing the schema for diagnostics data consumed through the
Archivist.

## Building

This project should be automatically included in builds.

## Using

`diagnostics-schema` can be used by depending on the
`//src/diagnostics/lib/diagnostics-schema` gn target and then using the `diagnostics_schema` crate
in a rust project.

`diagnostics-schema` is not available in the sdk and is intended to be used only by
diagnostics binaries.

## Testing

Unit tests for `diagnostics-schema` are available in the `diagnostics_schema_tests` package:

```
$ fx run-test diagnostics-schema-tests
```

You'll need to include `//src/diagnostics/lib/diagnostics-schema:tests` in your
build, either by using `fx args` to put it under `universe_package_labels`, or
by `fx set [....] --with //src/diagnostics/lib/diagnostics-schema:tests`.
