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

[test-runner]: ../README.md