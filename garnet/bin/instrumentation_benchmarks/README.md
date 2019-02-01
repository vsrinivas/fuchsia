# Instrumentation Benchmarks

Microbenchmarks for Inspect library (ExposedObject).

## Writing Benchmarks

This uses Zircon's
[perftest](https://fuchsia.googlesource.com/zircon/+/master/system/ulib/perftest/)
library.

## Running Benchmarks

There are two ways to run instrumentation_benchmarks:

* perftest mode: This mode will record the times taken by each run of
  the benchmarks, allowing further analysis, which is useful for
  detecting performance regressions.  It also prints some summary
  statistics.  This uses the test runner provided by Zircon's perftest
  library.

  For this, run
  `/pkgfs/packages/instrumentation_benchmarks/0/test/instrumentation_benchmarks -p
  --out=output.json`.  The result data will be written to
  `output.json` using our [perf test result schema].

  See Zircon's perftest library for details of the other command line
  options.

* Test-only mode: This runs on the bots via `runtests`, and it just checks
  that each benchmark still works.  It runs quickly -- it runs only a small
  number of iterations of each benchmark.  It does not print any
  performance information.

  For this, run
  `/pkgfs/packages/instrumentation_benchmarks/0/test/instrumentation_benchmarks`.

[perf test result schema]: https://fuchsia.googlesource.com/docs/+/master/development/benchmarking/results_schema.md
