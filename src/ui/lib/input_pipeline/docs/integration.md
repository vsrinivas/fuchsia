# input_pipeline > Integration

Reviewed on: 2022-03-22

This document describes how to integrate the input pipeline within a
larger Rust program. For example, the input pipeline is integrated with
* the [Scene Manager](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/bin/scene_manager/) for workstation builds,
* the [input pipeline component](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/bin/input-pipeline/) for other builds.

## Requirements
The code that deals with the input pipeline must run on a `LocalExecutor`. For
existing code, this is already true. Both `//src/ui/bin/input-pipeline` and
`//src/session/bin/scene_manager` use `fuchsia_async::run_singlethreaded`,
so they each use a `LocalExecutor`.

## Alternatives
A program might want to isolate itself from the risk that a bug in the input
pipeline library (e.g. infinite loop) will keep the rest of the program from
making progress. Such programs can
1. Spawn a dedicated thread to run a `LocalExecutor` which creates and runs
   the input pipeline, OR
1. Run the input pipeline as a standalone component, and communicate with
   the pipeline over FIDL.