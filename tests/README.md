This directory holds tests for the modular framework.

Tests here are integration tests, run through the
[test_runner](../src/test_runner/README.md) against a fully built fuchsia
instance running on either the build host (using QEMU) or on a target
device. The test runner discovers the running fuchsia instance automatically,
but may get confused if there is more than one.

NOTE: In order to run tests, the test_runner must be run at startup time. You
can either:

* Use a gn module to automatically start it: `./packages/gn/gen.py -m boot_test_modular`
* or, run `/system/apps/test_runner` on your booted device. This needs to be run
  in a Fuchsia environment, so if your prompt is `magenta$`, prefix it with
  `@ bootstrap ...`.

Each subdirectory contains one integration test, which can be run by invoking
its `test.sh` script.

All tests together can be run by invoking [test.sh](test.sh) in this directory.

A newly added test must be added to *both*
[modular_tests.json](modular_tests.json) and [test.sh](test.sh), which evidently
will change eventually.

All `test.sh` scripts require to set up the fuchsia environment by sourcing
[scripts/env.sh](https://fuchsia.googlesource.com/scripts/+/master/env.sh).

