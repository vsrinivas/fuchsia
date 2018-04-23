runbenchmarks
==

## Overview

`runbenchmarks` is a shell library for driving Fuchsia benchmarks.  Most of the
programs written using this library are used by Fuchsia infrastructure which
executes the benchmarks and uploads the results to the performance dashboard.

For example usage see `//garnet/bin/benchmarks`.

## API Reference

### runbench_exec

```sh
runbench_exec <results_file> <arg0> <arg1> ...
```

#### Description

Runs the command specified by `<arg0> <arg1> ...` and verifies that `<results_file>` was produced as a result.
