# `fuchsia_inspect`

`fuchsia_inspect` is a library for writing and reading Inspect-formatted
VMOs. Docs are available [here](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/docs/development/inspect/vmo-format/README.md).

## Building

This project should be automatically included in builds.

## Using

`fuchsia_inspect` can be used by depending on the
`//src/lib/inspect/rust/fuchsia-inspect` GN target and then using
the `fuchsia_inspect` crate in a Rust project.

`fuchsia_inspect` is not available in the SDK.

## Testing

Unit tests for `fuchsia_inspect` are available in the
`fuchsia_inspect_tests` package:

```
$ fx run-test fuchsia_inspect_tests
```

You'll need to include `//src/lib/inspect/rust/fuchsia-inspect:tests` in your
build, either by using `fx args` to put it under `universe_package_labels`, or
by `fx set [....] --with //src/lib/inspect/rust/fuchsia-inspect:tests`.

## Benchmarking

Benchmarks for `fuchsia_inspect` are available in the `rust_inspect_benchmarks`
package.

One way to run the benchmarks is with the following command:

```
$ fx shell trace record --spec-file=/pkgfs/packages/rust_inspect_benchmarks/0/data/benchmarks.tspec
```

It is possible to run the benchmarks in a fast "unit test mode" with
the following test command.  This runs a small number of test
iterations without collecting performance results, which can be useful
for checking that the tests don't fail:

```
$ fx shell run rust_inspect_benchmarks
```

You'll need to include `//src/lib/inspect/rust/fuchsia-inspect:benchmarks` in your
build, either by using `fx args` to put it under `universe_package_labels`, or
by `fx set [....] --with //src/lib/inspect/rust/fuchsia-inspect:benchmarks`.
