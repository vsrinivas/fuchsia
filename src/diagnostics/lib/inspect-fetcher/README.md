# `inspect_fetcher`

Last Reviewed: 2021-03-25

`inspect_fetcher` is a library for fetching Inspect data from Archivist
according to supplied selectors.

## Building

This project should be automatically included in builds.

## Using

`fuchsia_inspect` can be used by depending on the
`//src/diagnostics/lib/inspect-fetcher` gn target and then using
the `inspect_fetcher` crate in a rust project.

`inspect_fetcher` is not available in the sdk and is intended to be used only by
diagnostics binaries.

## Testing

unit tests for `inspect_fetcher` are available in the
`inspect_fetcher` package:

```
$ fx test inspect_fetcher_tests
```

You'll need to include `//src/diagnostics/lib/inspect-fetcher:tests` in your
build, either by using `fx args` to put it under `universe_package_labels`, or
by `fx set [....] --with //src/diagnostics/lib/inspect-fetcher:tests`.
