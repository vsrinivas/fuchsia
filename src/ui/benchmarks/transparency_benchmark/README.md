# Transparency Benchmark

This directory contains an application that draws a series of scenes that stress test the GPU, extracting some basic performance metrics involving bandwidth, fragment compute budget, and effective texture working set size.

This benchmark is still a work in progress.

## Running the example

TODO(https://fxbug.dev/105469): Add support for Flatland to this benchmark

This can be run in session_manager on any product where flatland is disabled. Add
`--args=use_flatland_by_default=false` to your `fx set` and run the benchmark:

```
$ session_control add fuchsia-pkg://fuchsia.com/transparency_benchmark#meta/transparency_benchmark.cm
```
