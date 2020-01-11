# `fuchsia_inspect_node_hierarchy`

`fuchsia_inspect_node_hierarchy` is a library that contains the Node Hierarchy that is read out of
an Inspect VMO. Docs are available [here](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/docs/development/inspect/vmo-format/README.md).

## Building

This project should be automatically included in builds.

## Using

`fuchsia_inspect_node_hierarchy` can be used by depending on the
`//src/lib/inspect/rust/fuchsia-inspect-node-hierarchy` GN target and then using
the `fuchsia_inspect_node_hierarchy` crate in a Rust project.

`fuchsia_inspect_node_hierarchy` is not available in the SDK.

## Testing

Unit tests for `fuchsia_inspect_node_hierarchy` are available in the
`fuchsia_inspect_node_hierarchy_tests` package:

```
$ fx run-test fuchsia_inspect_node_hierarchy_tests
```

You'll need to include `//src/lib/inspect/rust/fuchsia-inspect-node-hierarchy:tests` in your
build, either by using `fx args` to put it under `universe_package_labels`, or
by `fx set [....] --with //src/lib/inspect/rust/fuchsia-inspect-node-hierarchy:tests`.

## Benchmarking

Benchmarks for `fuchsia_inspect_node_hierarchy` are available in the `rust_inspect_benchmarks`
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

You'll need to include `//src/lib/inspect/rust/fuchsia-inspect-node-hierarchy:benchmarks` in your
build, either by using `fx args` to put it under `universe_package_labels`, or
by `fx set [....] --with //src/lib/inspect/rust/fuchsia-inspect-node-hierarchy:benchmarks`.
