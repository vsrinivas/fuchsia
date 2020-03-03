# Fuchsia Microbenchmarks

This set of tests includes microbenchmarks for Zircon syscalls and IPC
primitives, as well as microbenchmarks for other layers of Fuchsia.

This used to be called "zircon_benchmarks", but it was renamed to reflect
that it covers more than just Zircon.

## Writing Benchmarks

This uses Zircon's
[perftest](https://fuchsia.googlesource.com/fuchsia/+/master/zircon/system/ulib/perftest/)
library.

## Running Benchmarks

There are two ways to run fuchsia_microbenchmarks:

* perftest mode: This mode will record the times taken by each run of
  the benchmarks, allowing further analysis, which is useful for
  detecting performance regressions.  It also prints some summary
  statistics.  This uses the test runner provided by Zircon's perftest
  library.

  For this, run `fuchsia_microbenchmarks -p --out=output.json`. The result
  data will be written to `output.json` using our [perf test result
  schema].

  See Zircon's perftest library for details of the other command line
  options.

* Test-only mode: This runs on the bots via `runtests`, and it just checks
  that each benchmark still works.  It runs quickly -- it runs only a small
  number of iterations of each benchmark.  It does not print any
  performance information.

  For this, run
  `run-test-component fuchsia-pkg://fuchsia.com/fuchsia_microbenchmarks#meta/fuchsia_microbenchmarks.cmx`.

[perf test result schema]: /docs/development/benchmarking/results_schema.md
