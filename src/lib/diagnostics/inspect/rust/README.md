# `fuchsia_inspect`

`fuchsia_inspect` is a library for writing and reading Inspect-formatted
VMOs. Docs are available [here](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/docs/development/inspect/vmo-format/README.md).

## Building

This project should be automatically included in builds.

## Using

`fuchsia_inspect` can be used by depending on the
`//src/lib/diagnostics/inspect/rust` GN target and then using
the `fuchsia_inspect` crate in a Rust project.

`fuchsia_inspect` is not available in the SDK.

## Testing

Unit tests for `fuchsia_inspect` are available in the
`fuchsia_inspect_tests` package:

```
$ fx run-test fuchsia_inspect_tests
```

You'll need to include `//src/lib/diagnostics/inspect/rust:tests` in your
build, either by using `fx args` to put it under `universe_package_labels`, or
by `fx set [....] --with //src/lib/diagnostics/inspect/rust:tests`.

## Benchmarking

Benchmarks for `fuchsia_inspect` are available in the `rust_inspect_benchmarks`
package.

One way to run the benchmarks is with the following commands:

```
$ fx run-e2e-tests rust_inspect_benchmarks_test
$ fx run-e2e-tests rust_inspect_reader_benchmarks_test
```

It is possible to run the benchmarks in a fast "unit test mode" with
the following test commands.  These run a small number of test
iterations without collecting performance results, which can be useful
for checking that the tests don't fail:

```
$ fx shell run rust_inspect_benchmarks --benchmark writer
$ fx shell run rust_inspect_benchmarks --benchmark reader
```

You'll need to include `//src/lib/diagnostics/inspect/rust:benchmarks` in your
build, either by using `fx args` to put it under `universe_package_labels`, or
by `fx set [....] --with //src/lib/diagnostics/inspect/rust:benchmarks`.
