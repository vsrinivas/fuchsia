# Performance tests

This directory contains performance tests. In many cases, the code in this
directory is just an [SL4F] wrapper (that is, Dart code using the SL4F
framework) for a test that is located elsewhere in the Fuchsia source tree.

This directory contains the following performance tests:

*   `dart_inspect_benchmarks_test` - Tests the performance of the [Inspect]
    API from Dart.

*   `fidl_microbenchmarks_test` - SL4F wrapper for running FIDL
    microbenchmarks which test FIDL bindings for multiple languages.

*   `flutter` - Various Flutter performance tests.

*   `garnet_input_latency_benchmarks_test` - Tests the performance of
    end-to-end input latency, measured by tracing flow events, for minimal
    Scenic clients (`simplest_app` and `yuv_to_image_pipe`).

*   `kernel_boot_timeline_test` - SL4F wrapper for a test that records the
    time taken by different parts of the kernel boot process.

*   `microbenchmarks_test` - SL4F wrapper for [fuchsia_microbenchmarks].
    The microbenchmarks this contains tend to be for low-level operations
    such as Zircon syscalls.

*   `netstack_benchmarks_test` - SL4F wrapper for a benchmark of UDP.

*   `netstack_iperf_test` - Tests network stack performance using the
    benchmarking tool `iperf3`.

*   `rust_inspect_benchmarks_test` - Tests the performance of various Rust
    [Inspect] operations, such as creating and deleting nodes and updating
    properties.

*   `touch_input_latency_benchmarks_test` - Measures the latency of
    handling touch input events.

You can view the test results from CI builds on the [Catapult][catapult]
performance dashboard.

<!-- Reference links -->

[SL4F]: /docs/concepts/testing/sl4f.md
[Inspect]: /docs/development/inspect/README.md
[fuchsia_microbenchmarks]: /src/tests/microbenchmarks
[catapult]: /docs/development/benchmarking/catapult_user_guide.md
