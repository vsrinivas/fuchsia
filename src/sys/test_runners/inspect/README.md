# Inspect Test Runner

Reviewed on: 2021-04-22

Inspect test runner is a [test runner][test-runner] that supports
checking for the presence of specific Inspect properties on a system. The
properties are checked from the global-system Archivist, and these tests
should be run in isolation.

## Building

```bash
fx set core.x64 --with //src/sys/test_runners/inspect
fx build
```

## Examples

Examples to demonstrate how to write an Inspect test:

- [Sample test](test_data/sample-inspect-tests/meta/sample_inspect_tests.cml)

To run this example:

```bash
fx test sample-inspect-tests
```

## Concurrency

Test cases are executed concurrently, respecting the test suite `parallel`
option. The outcome of all tests are pumped together until the referenced
property is found or a configurable timeout is reached.

## Program Arguments

Each Inspect test exists under the "programs/tests" key in the component manifest with the following structure:

```
program: {
    accessor: "ALL",     // Possible values: ALL, LEGACY, FEEDBACK.
    timeout_seconds: 60, // Timeout for these keys in seconds.
    cases: [
      "core/appmgr:root:version",  // A string selector ensures that the given property is present at all.
      "bootstrap/archivist:fuchsia.inspect.Health WHERE [a] a == \"OK\"", // Arbitrary comparisons are supported using Triage format.
    ]
}
```

For more information about the value of the `expression` key, see the [Triage codelab][triage].

## Limitations

No known current limitations.

## Testing

Run:

```bash
fx test inspect-test-runner-unit-test

fx test inspect-runner-integration-test
```

## Source layout

The entrypoint is located in `src/main.rs`, the FIDL service implementation and
all the test logic exists in `src/test_server.rs`. Unit tests are co-located
with the implementation. Integration tests are located in `tests` folder.

[test-runner]: ../README.md
[triage]: /fuchsia-src/development/diagnostics/triage/codelab
