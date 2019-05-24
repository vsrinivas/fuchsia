# Perf Tests

These are Zircon's performance tests.

## Running

### Standalone Zircon Build

When using a standalone Zircon build the tests can be run with

`/boot/test/sys/perf-test`

### Fuchsia Build

When using a Fuchsia build you must first ensure the right package is
included. Try building with

`fx set core.x64 --with-base //bundles/buildbot:core && fx full-build`

You can then run the tests with

`/pkgfs/packages/garnet_benchmarks/0/test/sys/perf-test`

## Developing

For examples of how to use the testing framework, as well as coding guidelines,
please read:
[perftest.h](/system/ulib/perftest/include/perftest/perftest.h)
