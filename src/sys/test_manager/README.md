# test manager

Reviewed on: 2019-11-06

Test Manager runs v2 tests natively and returns result. This would eventually run directly under component manager and host side will talk to this tool to run v2 tests on device.

## Building

Test Executor should be included test build of Fuchsia, but if missing
can be added to builds by including `--with //src/sys/test_manager` to the
`fx set` invocation.

## Running

Current implementation run echo v2 tests. In future we will extend it to run any test.
```
$ fx shell run fuchsia-pkg://fuchsia.com/component_manager#meta/component_manager.cmx fuchsia-pkg://fuchsia.com/test_manager#meta/test_manager.cm
```

## Testing

Tests for this project are available in the `tests` folder.

```
$ fx run-test test_manager_tests
```

## Source layout

The entrypoint is located in `src/main.rs`. Tests live in `tests/`.
