# Netstack benchmarks

Netstack benchmarks are a set of test-application components with the
following objectives:

* Define and generate various netstack related benchmarking data as
  part of CI and publish them to Catapult-benchmarking dashboard.
* Enable regression monitoring in Catapult to catch any regressions in CI
  and raise monorail bugs.

## Micro benchmarks

These are benchmarks that are focused on benchmarking duration of
specific socket related system calls from the test application.

These can be run manually with:
```
fx test --e2e netstack_benchmarks_test
```

Your `fx set` line should contain:
```
--with //src/tests/end_to_end/perf:test
```
