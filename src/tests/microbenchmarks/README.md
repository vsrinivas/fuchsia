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

This uses the [perftest C++ library][perftest].

## Running Benchmarks

fuchsia_microbenchmarks can be run the following ways:

*   **Maximal:** You can use the
    [`microbenchmarks_test`][microbenchmarks_test] entry point used in
    Infra builds on CI and CQ.  For this, follow the instructions in
    [How to run performance tests][running-perf-tests] and use this
    command:

    ```
    fx test --e2e host_x64/microbenchmarks_test
    ```

    This entry point is fairly slow because it runs the
    fuchsia_microbenchmarks process multiple times.

    This entry point will copy the performance results
    ([`*.fuchsiaperf.json`][fuchsiaperf] files) back to the host in
    `out/test_out` directory.

*   **Minimal:** Unit test mode.  To check that the tests pass,
    without collecting any performance results, you can run the
    fuchsia_microbenchmarks component directly:

    ```
    ffx test run fuchsia-pkg://fuchsia.com/fuchsia_microbenchmarks#meta/fuchsia_microbenchmarks.cm
    ```

    This runs quickly because it runs only a small number of
    iterations of each benchmark.

*   **In-between:** If you want to run the tests differently from the
    [`microbenchmarks_test`][microbenchmarks_test] entry point above,
    such as to run a smaller or faster set of tests, you can invoke
    the fuchsia_microbenchmarks component directly and pass some of
    the options accepted by the [perftest C++ library][perftest].

    For example, the following invocation runs only the `Syscall/Null`
    test case and prints some summary statistics:

    ```
    ffx test run fuchsia-pkg://fuchsia.com/fuchsia_microbenchmarks#meta/fuchsia_microbenchmarks.cm -- -p --filter '^Syscall/Null$'
    ```

    The following invocation will produce an output file in
    [`fuchsiaperf.json`][fuchsiaperf] format and will copy it back to
    the host side:

    ```
    ffx test run fuchsia-pkg://fuchsia.com/fuchsia_microbenchmarks#meta/fuchsia_microbenchmarks.cm --output-directory host-output-dir -- -p --out /custom_artifacts/results.fuchsiaperf.json
    ```

    An alternative is to modify the `microbenchmarks_test` wrapper
    program locally to change the set of tests it runs or the number
    of iterations it runs.

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

[perftest]: /zircon/system/ulib/perftest/README.md
[microbenchmarks_test]: /src/tests/end_to_end/perf/test/microbenchmarks_test.dart
[running-perf-tests]: /docs/development/performance/running_performance_tests.md
[fuchsiaperf]: /docs/development/performance/fuchsiaperf_format.md
[direct-mode]: https://fuchsia-review.googlesource.com/c/fuchsia/+/684845
