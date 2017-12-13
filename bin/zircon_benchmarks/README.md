# Zircon Benchmarks

Microbenchmarks for the Zircon kernel and services.

There are three ways to run zircon_benchmarks:

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

* fbenchmark mode (Fuchsia benchmarks): This mode will record the times
  taken by each run of the benchmarks, allowing further analysis, which is
  useful for detecting performance regressions.  This uses the test runner
  code in `test_runner.cc`.

  For this, run `zircon_benchmarks --fbenchmark_out=output.json`.  The
  result data will be written to `output.json`.  This uses the JSON output
  format described in the [Fuchsia Tracing Usage
  Guide](../../docs/tracing_usage_guide.md#benchmark-result-export).

  Pass `--fbenchmark_runs=N` to change the number of times each benchmark
  is run.

  Note: Not all of the benchmarks have been converted so that they will run
  in this mode.  (TODO(TO-651): Convert the remaining tests.)  Those that
  have been converted will run in both fbenchmark mode and gbenchmark mode.

* Test-only mode: This runs on the bots via `runtests`, and it just checks
  that each benchmark still works.  It runs quickly -- it runs only a small
  number of iterations of each benchmark.  It does not print any
  performance information.

  For this, run `/system/test/zircon_benchmarks_test`.
