# Gunit Runner

Reviewed on: 2022-04-07

Gunit Runner is a [test runner][test-runner] that launches a gunit binary as a
component, parses its output, and translates it to fuchsia.test.Suite protocol
on behalf of the test.

## Building

```bash
fx set core.x64 --with //src/sys/test_runners/gunit
fx build
```

## Example

A test that needs additional capabilities can use a manifest like the following:

```
{
    include: [
        "//src/sys/test_runners/gunit/default.shard.cml",
    ],
    program: {
        binary: "bin/my_component_test",
    },
    // ... other capabilities
}
```
## Concurrency

Test cases are executed sequentially by default.
[Instruction to override][override-parallel].

## Arguments

See [passing arguments][passing-arguments] to learn more.

## Limitations

-   If a test calls `GUNIT_SKIP()`, it will be recorded as `Passed` rather than
    as `Skipped`. This is due to a bug in gunit itself.

## Testing

We currently don't have a way of writing gunit tests. So we simulate those tests
by converting gunit flags to gtest flags and then running the test using gtest
framework.

Run:

```bash
fx test gunit_runner_tests
fx test gunit-runner-integration-test
fx test gunit-runner-smoke-test

```

## Source layout

Gtest and Gunit have identical output formats. They differ in name of the flags
passed to test binary. All source code of this component is hosted in
`//src/sys/test_runners/gtest` folder. This folder hosts configuration, build
files and some test files.

The entrypoint is located in `src/main.rs`, the FIDL service implementation and
all the test logic exists in `src/test_server.rs`. Unit tests are co-located
with the implementation.

[test-runner]: ../README.md
[override-parallel]: https://fuchsia.dev/fuchsia-src/development/testing/components/test_runner_framework#controlling_parallel_execution_of_test_cases
[passing-arguments]: https://fuchsia.dev/fuchsia-src/development/testing/components/test_runner_framework#passing_arguments
