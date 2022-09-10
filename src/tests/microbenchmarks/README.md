# Fuchsia Microbenchmarks

This set of tests includes microbenchmarks for core OS functionality,
including Zircon syscalls and IPC primitives, as well as microbenchmarks for
other layers of Fuchsia.

Some of the microbenchmarks are portable and can be run on Linux or Mac, for
comparison against Fuchsia.  This means that the name fuchsia_microbenchmarks
should be taken to mean "microbenchmarks that are part of the Fuchsia project"
rather than "microbenchmarks that only run on Fuchsia".

This used to be called "zircon_benchmarks", but it was renamed to reflect
that it covers more than just Zircon.

## Writing Benchmarks

This uses Zircon's
[perftest](https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/ulib/perftest/)
library.

## Running Benchmarks

There are two ways to run fuchsia_microbenchmarks:

* perftest mode: This mode will record the times taken by each run of
  the benchmarks, allowing further analysis, which is useful for
  detecting performance regressions.  It also prints some summary
  statistics.  This uses the test runner provided by Zircon's perftest
  library.

  For this, run `fuchsia_microbenchmarks -p --out=output.json`. The result
  data will be written to `output.json` in the [Fuchsiaperf format].

  See Zircon's perftest library for details of the other command line
  options.

* Test-only mode: This runs on the bots via `runtests`, and it just checks
  that each benchmark still works.  It runs quickly -- it runs only a small
  number of iterations of each benchmark.  It does not print any
  performance information.

  For this, run
  `run-test-component fuchsia-pkg://fuchsia.com/fuchsia_microbenchmarks#meta/fuchsia_microbenchmarks.cmx`.

[Fuchsiaperf format]: /docs/development/performance/fuchsiaperf_format.md


## Direct Mode Microbenchmarks

<!-- TODO(fxbug.dev/95763): Update the link below once the RFC lands. -->
[Direct mode][direct-mode] is a way to run unmodified Fuchsia binaries under
Virtualization. These binaries have direct access to Zircon syscalls and can be
agnostic to whether they are running under direct mode.

The direct mode microbenchmarks are intended to be used to compare performance
of direct mode to regular execution. This can help us guide optimisations, as
well as to highlight regressions, while we develop direct mode.

To run the benchmarks as a unit test, use:

`ffx test run fuchsia-pkg://fuchsia.com/direct_mode_microbenchmarks#meta/direct_mode_microbenchmarks.cm`

Or, as a performance test, use:

`ffx test run fuchsia-pkg://fuchsia.com/direct_mode_microbenchmarks#meta/direct_mode_microbenchmarks.cm -- -p`

Or, as a quick performance test, use:

`ffx test run fuchsia-pkg://fuchsia.com/direct_mode_microbenchmarks#meta/direct_mode_microbenchmarks.cm -- -p --runs 5`

<!-- Links -->

[direct-mode]: https://fuchsia-review.googlesource.com/c/fuchsia/+/684845
