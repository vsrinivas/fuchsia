runbenchmarks
==

## Overview

`runbenchmarks` is a shell library for driving Fuchsia benchmarks.  Most of the
programs written using this library are used by Fuchsia infrastructure which
executes the benchmarks and uploads the results to the performance dashboard.

For example usage see `//garnet/tests/benchmarks`.

## API Reference

### runbench_exec

```sh
runbench_exec <results_file> <arg0> <arg1> ...
```

#### Description

Runs the command specified by `<arg0> <arg1> ...` and verifies that `<results_file>`
was produced as a result.

### runbench_exit

```sh
runbench_exit <output_dir>
```

#### Description

Exits the current process, performing any cleanup work necessary. `<output_dir>` should
be the same directory that test results were written to.
