# GTest Runner

Reviewed on: 2020-04-20

GTest Runner is a [test runner][test-runner] that launches a gtest binary as a
component, parses its output, and translates it to fuchsia.test.Suite protocol
on behalf of the test.

## Building

```bash
fx set core.x64 --with //src/sys/test_runners/gtest
fx build
```

## Examples

Examples to demonstrate how to write v2 test

-   [Simple test](meta/sample_tests.cml)

To run this example:

```bash
fx test gtest-runner-example-tests
```

## Concurrency

Test cases are executed sequentially by default.
[Instruction to override][override-parallel].

## Arguments

See [passing arguments][passing-arguments] to learn more.

## Limitations

-   If a test calls `GTEST_SKIP()`, it will be recorded as `Passed` rather than
    as `Skipped`.
    This is due to a bug in gtest itself.

## Testing

Run:

```bash
fx test gtest_runner_tests
```

## Source layout

The entrypoint is located in `src/main.rs`, the FIDL service implementation and
all the test logic exists in `src/test_server.rs`. Unit tests are co-located
with the implementation.

[test-runner]: ../README.md
[override-parallel]: https://fuchsia.dev/fuchsia-src/development/testing/components/test_runner_framework#controlling_parallel_execution_of_test_cases
[passing-arguments]: https://fuchsia.dev/fuchsia-src/development/testing/components/test_runner_framework#passing_arguments
