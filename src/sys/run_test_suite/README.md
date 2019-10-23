# test executor

Reviewed on: 2019-10-16

Test Executor runs tests which implement `fuchsia.test.Suite` and displays result.
It will exit with code 0 if tests passes else with code 1.

## Building

Test Executor should be included test build of Fuchsia, but if missing
can be added to builds by including `--with //src/sys/test_executor` to the
`fx set` invocation.

## Running

```
$ fx shell run fuchsia-pkg://fuchsia.com/test_executor#meta/test_executor.cmx <v2_test_component_url>
```

## Testing

Tests for this project are available in the `tests` folder.

```
$ fx run-test test_executor_integration_tests
```

## Source layout

The entrypoint is located in `src/main.rs`. Integration tests
live in `tests/`.
