# FIDL Micro Benchmarks

### Running benchmarks the "new way" (SL4F based):

    fx set terminal.x64
    fx run-e2e-tests fidl_microbenchmarks_test

Note that you must have `fx set` to a product that supports the perf end to end tests. Currently,
this is `terminal` and `workstation`, but refer to the end to end tests reference page for up to
date information.

Individual FIDL benchmarks can be run using the `-n` flag:

    fx run-e2e-test fidl_microbenchmarks_test -- -n "rust_fidl_microbenchmarks"
