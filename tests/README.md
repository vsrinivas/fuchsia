# Modular integration tests

Tests here are integration tests, run through the [Test Runner][test_runner]
framework in a fuchsia instance running on either the build host using QEMU or
on a target device.

There are two ways to invoke the test suite, remotely from the build host or
directly on the fuchsia instance.

## Starting tests remotely from the build host

The test runner discovers the running fuchsia instance automatically, but will
stop with an error if there is more than one. To fix the problem, specify the
device name with the `--server` parameter.

In order to run tests from your workstation, a `test_runner` must be running
under fuchsia. In order to start `test_runner`, you can:

* Either, use a gn module to automatically start `test_runner` at fuchsia boot
  time using [boot_test.config](boot_test.config). At build time on your
  workstation, do:

```
./packages/gn/gen.py -m boot_test_modular

```

* Or else, invoke `test_runner` manually after starting fuchsia. In a shell on
  your fuchsia device, do:

```
/system/apps/test_runner
```

Each subdirectory of `apps/modular/tests` contains one integration test. They
are all run together by the test runner as specified by the [modular_tests.json]
configuration file:

```
$FUCHSIA_DIR/apps/test_runner/src/run_test \
  --test_file=$FUCHSIA_DIR/apps/modular/tests/modular_tests.json --sync
```

The option `--sync` is optional and causes required binaries that have changed
since the fuchsia was started to be updated before the tests are executed.

## Starting the test suite directly under fuchsia

`run_modular_tests.sh` is a command that runs all of the Modular tests. It is
based on the [Test Runner][test_runner] framework, but runs the tests
directly. So no `test_runner` is required to be running under fuchsia.

It can be run directly from the fuchsia shell:

```
$ /system/test/run_modular_tests.sh
```


[test_runner]: https://fuchsia.googlesource.com/test_runner/ "Test Runner"
