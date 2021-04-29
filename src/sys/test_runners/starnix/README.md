# Starnix Test Runner

Reviewed on: 2021-04-14

Starnix Test Runner is a [test runner][test-runner] that launches a component
using the Starnix runner.

This test runner is useful for running test binaries that are compiled for Linux.

Currently this test runner is in early stages of development, and does not perform
any verification on the output from the test component.

## Building

```bash
fx set core.x64 --with //src/sys/test_runners/starnix
fx build
```

# Starnix Unit Test Runner

This runner is intended to be used by the starnix runner's unit tests. It is a
wrapper around the regular Rust test runner that adds additional permissions
required by starnix unit tests that shouldn't be available to regular Rust test
components.

[test-runner]: ../README.md