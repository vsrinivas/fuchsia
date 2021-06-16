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
$ fx test test_manager_tests
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


