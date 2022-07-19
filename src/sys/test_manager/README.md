# test manager

Reviewed on: 2022-07-19

Test Manager runs v2 tests natively and returns result. [`run-test-suite`][rts]
and [`ffx test`][ffx-test] invoke Test Manager to run tests.

## Building

Test Manager should be included in eng builds of Fuchsia, but if missing
can be added to builds by including `--with //src/sys/test_manager` to the
`fx set` invocation.

## Running

Test Manager is invoked by either [`run-test-suite`][rts] or [`ffx test`][ffx-test]
when these tools are used to run tests. Test Manager should not be run directly.

## Testing

Tests for this project are available in the `tests` folder.
To run them include the test to your build by adding
`--with //src/sys/test_manager:tests` to the `fx set` invocation.

The full set of unit tests and integration tests can be run with

```
$ fx test //src/sys/test_manager
```

To run the integration tests only, run

```
$ fx test test_manager_test
```

## Source layout

The entrypoint is located in `src/main.rs`. Tests live in `tests/`.

## Development

When making changes to `test manager` or its [children][test-manager-cml],
developers want to run their tests against latest version of code. The following
section highlights various scenarios and action to take to load the latest
version of changes:

### Changes to test manager code

When changes are made to test manager code, they can be loaded by first killing
test manager on the device and then running the test.

```
fx shell killall test_manager.cm
```

### Changes to test manager's child code

When changes are made to test manager's static child's code, developer first
needs to kill that child and then run their test. For eg if changes are made to
gtest_runner code:

```
fx shell killall gtest_runner.cm
```

### Changes to manifest

When changes are made to test manager or its static child manifest file, the
device should be rebooted before the changes can be loaded.

```
fx reboot
```

### Changes to a test

When changes are made to a test, the test can be executed again, and the latest
version will be loaded and executed.

```
fx test <test_url>
```


[test-manager-cml]: meta/common.shard.cml
[rts]: src/sys/run_test_suite
[ffx-test]: src/developer/ffx/plugins/test
