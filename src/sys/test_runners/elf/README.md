# ELF Test Runner

Reviewed on: 2020-12-21

ELF Test Runner is a [test runner][test-runner] that launches an ELF binary as
a component, waits for it to exit, and translates its process return code as
test status (passed for zero, failed for non-zero).

This test runner is useful for tests that don't rely on a particular test
framework, or for tests that rely on an unsupported framework. Because the
contract with the test is very simple, this test runner is very flexible and
can be used in many circumstances, but it offers the bare minimum features.
For instance tests that use this runner can't enumerate test cases, instead
they only have one synthetic test case named "main".

## Building

```bash
fx set core.x64 --with //src/sys/test_runners/elf
fx build
```

## Arguments

Test authors can specify command line arguments to their tests in their
component manifest file. These will be passed to the test when it is run.

## Testing

Run:

```bash
fx run-test elf_runner_tests
```

## Source layout

The entrypoint is located in `src/main.rs`, the FIDL service implementation and
all the test logic exists in `src/test_server.rs`. Unit tests are co-located
with the implementation.

[test-runner]: ../README.md
