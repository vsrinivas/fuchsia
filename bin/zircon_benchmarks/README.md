# Zircon Benchmarks

Microbenchmarks for the Zircon kernel and services.

## Writing Benchmarks

Use Zircon's
[perftest](https://fuchsia.googlesource.com/zircon/+/master/system/ulib/perftest/)
library.

You may notice that some benchmarks still use the gbenchmark library
(i.e. they include benchmark/benchmark.h). *Use of gbenchmark is
deprecated* so don't add any new ones.

perftest has several advantages over gbenchmark including testing
benchmarks on CQ and support for uploading results to the Catapult
performance dashboard.

## Running Benchmarks

There are three ways to run zircon_benchmarks:

* perftest mode: This mode will record the times taken by each run of
  the benchmarks, allowing further analysis, which is useful for
  detecting performance regressions.  It also prints some summary
  statistics.  This uses the test runner provided by Zircon's perftest
  library.

  For this, run `zircon_benchmarks -p --out=output.json`.  The result
  data will be written to `output.json`.  This uses the JSON output
  format described in the
  [Benchmarking](../../docs/benchmarking.md#export) guide.

  See Zircon's perftest library for details of the other command line
  options.

  Note: Not all of the benchmarks have been converted so that they will run
  in this mode.  (TODO(TO-651): Convert the remaining tests.)  Those that
  have been converted will run in both fbenchmark mode and gbenchmark mode.

* Test-only mode: This runs on the bots via `runtests`, and it just checks
  that each benchmark still works.  It runs quickly -- it runs only a small
  number of iterations of each benchmark.  It does not print any
  performance information.

  For this, run `/system/test/zircon_benchmarks_test`.

* gbenchmark mode: This uses the [Google benchmark library
  (gbenchmark)](https://github.com/google/benchmark).  This is the default.

  For this, run `zircon_benchmarks` with no arguments, or with arguments
  accepted by gbenchmark (such as `--help`).

  By default, this mode is quite slow to run, because gbenchmark uses a
  high default value for its `--benchmark_min_time` setting.  You can speed
  up gbenchmark by passing `--benchmark_min_time=0.01`.

  Note: gbenchmark's use of statistics is not very sophisticated, so this
  mode might not produce consistent results across runs for some
  benchmarks.  Furthermore, gbenchmark does not output any measures of
  variability (such as standard deviation).  This limits the usefulness of
  gbenchmark for detecting performance regressions.
