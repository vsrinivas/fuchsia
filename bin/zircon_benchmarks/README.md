# Zircon Benchmarks

Microbenchmarks for the Zircon kernel and services.

## Writing Benchmarks

This uses Zircon's
[perftest](https://fuchsia.googlesource.com/zircon/+/master/system/ulib/perftest/)
library.

## Running Benchmarks

There are two ways to run zircon_benchmarks:

* perftest mode: This mode will record the times taken by each run of
  the benchmarks, allowing further analysis, which is useful for
  detecting performance regressions.  It also prints some summary
  statistics.  This uses the test runner provided by Zircon's perftest
  library.

  For this, run
  `/pkgfs/packages/zircon_benchmarks/0/test/zircon_benchmarks -p
  --out=output.json`.  The result data will be written to
  `output.json`.  This uses the JSON output format described in the
  [Benchmarking](../../docs/benchmarking.md#export) guide.

  See Zircon's perftest library for details of the other command line
  options.

* Test-only mode: This runs on the bots via `runtests`, and it just checks
  that each benchmark still works.  It runs quickly -- it runs only a small
  number of iterations of each benchmark.  It does not print any
  performance information.

  For this, run
  `/pkgfs/packages/zircon_benchmarks/0/test/zircon_benchmarks`.
