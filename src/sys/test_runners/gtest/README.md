# GTest Runner

Reviewed on: 2020-03-11

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

- [Simple test](meta/sample_tests.cml)

To run this example:

```bash
fx run-test gtest_runner_example_tests
```

## Limitations

Currently gtest runner doesn't support:

- Disabled tests.
- Tests writing to stdout, those tests can be executed but stdout is lost.

## Testing

Run:

```bash
fx run-test gtest_runner_tests
```

## Source layout

The entrypoint is located in `src/main.rs`, the FIDL service implementation and
all the test logic exists in `src/test_component.rs`. Unit tests are co-located
with the implementation.

[test-runner]: ../README.md
