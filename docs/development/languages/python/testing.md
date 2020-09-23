# Python testing

The Fuchsia build system does not support python tests explicitly, but you can
define a host test and make it runnable using `fx test`, in CI and CQ with the
following steps:

*   Use the [host_test](/build/testing/host_test.gni) and
    [host_test_data](/build/testing/host_test_data.gni) GN templates.
*   Include all sources in the `host_test_data`.
*   Ensure some `group("tests")` depends on the `host_test` rule,
    and specify the `($host_toolchain)` in the dependency.

[Here](/cts/build/BUILD.gn) is an example BUILD.gn.
