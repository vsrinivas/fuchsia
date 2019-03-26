# Transparency Benchmark

This directory contains an application that draws a series of scenes that stress test the GPU, extracting some basic performance metrics involving bandwidth, fragment compute budget, and effective texture working set size.

This benchmark is still a work in progress.

## Running the example

From garnet, this can be run with:
```
present_view fuchsia-pkg://fuchsia.com/transparency_benchmark#meta/transparency_benchmark.cmx
```

From topaz, this can be run with:

```
sessionctl add_mod fuchsia-pkg://fuchsia.com/transparency_benchmark#meta/transparency_benchmark.cmx
```

In garnet, `Alt`+`Esc` toggles back and forth between the console and graphics.
