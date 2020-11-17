# `diagnostics_hierarchy`

`diagnostics_hierarchy` is a library that contains the Diagnostics Hierarchy that contains Inspect,
Logs or Lifecycle data.

## Building

This project should be automatically included in builds.

## Using

`diagnostics_hierarchy` can be used by depending on the
`//src/lib/diagnostics/hierarchy/rust` GN target and then using
the `diagnostics_hierarchy` crate in a Rust project.

`diagnostics_hierarchy` is not available in the SDK.

## Testing

Unit tests for `diagnostics_hierarchy` are available in the
`diagnostics_hierarchy_tests` package:

```
$ fx run-test diagnostics_hierarchy_tests
```

You'll need to include `//src/lib/diagnostics/hierarchy/rust:tests` in your
build, either by using `fx args` to put it under `universe_package_labels`, or
by `fx set [....] --with //src/lib/diagnostics/hierarchy/rust:tests`.

## Benchmarking

Benchmarks for `diagnostics_hierarchy` are available in the `rust_inspect_benchmarks`
package.

One way to run the benchmarks is with the following command:

```
$ fx run-e2e-tests rust_inspect_benchmarks_test
```

It is possible to run the benchmarks in a fast "unit test mode" with
the following test command.  This runs a small number of test
iterations without collecting performance results, which can be useful
for checking that the tests don't fail:

```
$ fx shell run rust_inspect_benchmarks --benchmark writer
```

You'll need to include `//src/lib/diagnostics/hierarchy/rust:benchmarks` in your
build, either by using `fx args` to put it under `universe_package_labels`, or
by `fx set [....] --with //src/lib/diagnostics/hierarchy/rust:benchmarks`.
