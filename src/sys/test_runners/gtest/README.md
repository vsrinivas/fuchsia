# GTest Runner

Reviewed on: 2022-03-09

GTest Runner is a [test runner][test-runner] that launches a gtest binary as a
component, parses its output, and translates it to fuchsia.test.Suite protocol
on behalf of the test.

## Building

```bash
fx set core.x64 --with //src/sys/test_runners/gtest
fx build
```

## Example

A test that needs additional capabilities can use a manifest like the following:

```
{
    include: [
        "//src/sys/test_runners/gtest/default.shard.cml",
    ],
    program: {
        binary: "bin/my_component_test",
    },
    // ... other capabilities
}
```

If the test uses death checks, such as `ASSERT_DEATH` or `EXPECT_DEATH`, the
test needs extra capabilities which can be requested with the `death_test`
shard:

```
{
    include: [
        "//src/sys/test_runners/gtest/death_test.shard.cml",
        "//src/sys/test_runners/gtest/default.shard.cml",
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
