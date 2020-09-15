# Performance tests

This test directory contains the following performance tests:

*   `garnet_input_latency_benchmarks_test` - It tests the performance of
    end-to-end input latency, measured by tracing flow events, for minimal
    Scenic clients (`simplest_app` and `yuv_to_image_pipe`).
*   `microbenchmarks_test` - It performs microbenchmarks testing for various low
    level Zircon operations and some operations close to Zircon (for instance,
    `malloc`).
*   `rust_inspect_benchmarks_test` - It tests the performance of various Rust
    inspect operations, such as creating and deleting nodes and updating
    properties.

You can view the test results from CI builds on the [Catapult][catapult]
performance dashboard.

<!-- Reference links -->

[catapult]: /docs/development/benchmarking/catapult_user_guide.md
