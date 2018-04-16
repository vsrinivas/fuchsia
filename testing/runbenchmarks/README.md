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
runbench_exec <results_file> <command>
```

#### Description

Runs `<command>` as the benchmark executable, using `<results_file>` as the name
of the file to write results to. `<command>` must accept a `--results-file` flag
for this to work.

### runbench_trace

```sh
runbench_trace <results_file> <tspec_file>
```

#### Description

Runs a tracing-based benchmark using `<tspec-file>` as the .tspec file and
`<results_file>` as the file to write results to. See
https://fuchsia.googlesource.com/garnet/+/master/docs/benchmarking.md for
more information about tspec files and tracing-based benchmarks.

