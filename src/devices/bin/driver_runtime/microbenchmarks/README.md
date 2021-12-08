# Fuchsia Microbenchmarks

This set of tests includes microbenchmarks for Driver Runtime IPC primitives.

## Writing Benchmarks

This uses Zircon's
[perftest](https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/ulib/perftest/)
library.

## Running Benchmarks

`ffx component run fuchsia-pkg://fuchsia.com/driver_runtime_microbenchmarks#meta/driver_runtime_microbenchmarks.cm --recreate`.
