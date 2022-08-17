# Performance tests

This directory contains performance tests. In many cases, the code in this
directory is just an [SL4F] wrapper (that is, Dart code using the SL4F
framework) for a test that is located elsewhere in the Fuchsia source tree.

This directory contains the following performance tests:

*   `audio_mixer_profiler_test` - SL4F wrapper for profiling the different
    steps in the audio mixing process.

*   `dart_inspect_benchmarks_test` - Tests the performance of the [Inspect]
    API from Dart.

*   `fidl_microbenchmarks_test` - SL4F wrapper for running FIDL
    microbenchmarks which test FIDL bindings for multiple languages.

*   `flatland_benchmarks_test` - Tests the performance of Flatland's
    end-to-end present latency, measured by tracing flow events, for minimal
    Flatland client (`flatland-view-provider`).

*   `input_latency_benchmarks_test` - Tests the performance of
    end-to-end input latency, measured by tracing flow events, for minimal
    Scenic clients (`simplest_app`).

*   `kernel_boot_stats_test` - SL4F wrapper for a test that records the
    time taken by different parts of the kernel boot process.

*   `microbenchmarks_test` - SL4F wrapper for [fuchsia_microbenchmarks].
    The microbenchmarks this contains tend to be for low-level operations
    such as Zircon syscalls.

*   `netstack_benchmarks_test` - SL4F wrapper for benchmarks of TCP, UDP, and
    ICMP echo sockets. These benchmarks run against Netstack2, Netstack2 with
    Fast UDP enabled, and a "fake netstack" that does minimal work in order to
    isolate the API overhead.

*   `netstack_iperf_test` - Tests network stack performance using the
    benchmarking tool `iperf3`.

*   `rust_inspect_benchmarks_test` - Tests the performance of various Rust
    [Inspect] operations, such as creating and deleting nodes and updating
    properties.

You can view the test results from CI builds in [Chromeperf][chromeperf].

<!-- Reference links -->

[SL4F]: /docs/concepts/testing/sl4f.md
[Inspect]: /docs/development/inspect/README.md
[fuchsia_microbenchmarks]: /src/tests/microbenchmarks
[chromeperf]: /docs/development/performance/chromeperf_user_guide.md
