# Golang Test Runner

Reviewed on: 2020-08-11

Golang test runner is a [test runner][test-runner] that launches a golang test
binary as a component, parses its output, and translates it to the
`fuchsia.test.Suite` protocol on behalf of the test.

## Building

```bash
fx set core.x64 --with //src/sys/test_runners/gotests
fx build
```

## Examples

Examples to demonstrate how to write v2 test:

- [Sample test](test_data/sample_go_test/meta/sample_go_test.cml)

To run this example:

```bash
fx test go-test-runner-example
```

## Limitations

### Test Enumeration

Only top level tests can be enumerated. Golang doesn't give us a way to
enumerate sub-tests.

### Disabled Tests

There is no way in golang to enumerate disabled tests, so all the tests will
be marked as enabled.


## Testing

Run:

```bash
fx test go-test-runner-unit-tests
```

## Source layout

The entrypoint is located in `src/main.rs`, the FIDL service implementation and
all the test logic exists in `src/test_server.rs`. Unit tests are co-located
with the implementation.

[test-runner]: ../README.md
