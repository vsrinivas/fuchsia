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

In order to run tests from your workstation, the `test_runner` must be running
under fuchsia. You can either:

* Use a gn module to automatically start it
  using [boot_test.config](boot_test.config). At build time, do:

```
./packages/gn/gen.py -m boot_test_modular

```

* Run it after starting fuchsia. At your `magenta$` prompt, do:

```
/system/apps/test_runner
```

Each subdirectory contains one integration test, which can be run by invoking
its `test.sh` script.

All tests together can be run by invoking [test.sh](test.sh) in this directory.
A new test must be added to [modular_tests.json](modular_tests.json) for it to
be run by `test.sh`.

All `test.sh` scripts require setting up the fuchsia environment by
sourcing [scripts/env.sh][env_sh].

## Starting the test suite directly under fuchisa

`run_modular_tests` is a command that runs all of the Modular tests. It is based
on the [Test Runner][test_runner] framework.

It can be run directly from either the shell:

```
$ /system/test/run_modular_tests
```


[test_runner]: https://fuchsia.googlesource.com/test_runner/ "Test Runner"
[env_sh]: https://fuchsia.googlesource.com/scripts/+/master/env.sh "scripts/env.sh"
