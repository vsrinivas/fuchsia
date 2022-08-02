# Scenic Image Grid

This directory contains a simple application which draws and animates a
collection of cards. Used for benchmarking purposes.

## USAGE

TODO(https://fxbug.dev/105469): Add support for Flatland to this benchmark

This can be run in session_manager on any product where flatland is disabled. Add
`--args=use_flatland_by_default=false` to your `fx set` and run the benchmark:

```
$ session_control add fuchsia-pkg://fuchsia.com/image_grid_cpp#meta/image_grid_cpp.cm
```
