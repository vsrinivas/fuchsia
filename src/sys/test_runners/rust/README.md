# Rust Test Runner

Reviewed on: 2020-04-20

Rust test runner is a [test runner][test-runner] that launches a rust test binary as a component, parses its output, and translates it to the `fuchsia.test.Suite` protocol on behalf of the test.

## Building

```bash
fx set core.x64 --with //src/sys/test_runners/rust
fx build
```

## Examples

Examples to demonstrate how to write v2 test:

- [Sample test](test_data/sample-rust-tests/meta/sample_rust_tests.cml)

To run this example:

```bash
fx run-test rust-test-runner-example
```

## Concurrency

Test cases are executed concurrently (max 10 test cases at a time by default).
[Instruction to override][override-parallel].

## Limitations

No known current limitations.

## Testing

Run:

```bash
fx run-test rust-test-runner-unit-test

fx run-test rust-runner-integration-test
```

## Source layout

The entrypoint is located in `src/main.rs`, the FIDL service implementation and
all the test logic exists in `src/test_server.rs`. Unit tests are co-located
with the implementation. Integration tests are located in `tests` folder.

[test-runner]: ../README.md
[override-parallel]: /docs/concepts/testing/test_component.md#running-test-cases-in-parallel
