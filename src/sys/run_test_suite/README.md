# run-test-suite

Reviewed on: 2021-06-15

Run test suite runs tests which implement `fuchsia.test.Suite` and displays result.
It will exit with code 0 if tests passes else with code 1.

## Building

Run test suite should be included test build of Fuchsia, but if missing
can be added to builds by including `--with //src/sys/run_test_suite` to the
`fx set` invocation.

## Running

```
$ fx shell run run-test-suite <v2_test_component_url>
```

## Testing

Tests for this project are available in the `tests` folder.

```
$ fx test run_test_suite_integration_tests
```

## Source layout

The entrypoint is located in `src/main.rs`. Integration tests
live in `tests/`.
