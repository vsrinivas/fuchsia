# Starnix Test Runners

Reviewed on: 2021-04-14

This directory contains two [test runners][test-runner]:

  * `starnix_test_runner`: runs test binaries that are compiled for Linux.
  * `starnix_unit_test_runner`: runs unit tests for the Starnix runner itself.

## Starnix Test Runner

The Starnix test runner runs test binaries that are compiled for Linux. Each
such Linux binary expects to run in a particular environment (e.g., a particular
system image). Starnix calls such an environment a "galaxy."

All galaxies share the same `starnix_runner.cml`, but they differ in their
configuration. A galaxy is simply a package that contains the Starnix runner
component along with a system image and configuration file.

The Starnix test runner expects each test component to bundle its galaxy inside
of the test package. The Starnix test runner then instantiates the bundled
Starnix runner for each test, and uses that runner to actually run the test
binary.

This means that the same test runner can be used for all Starnix galaxies, and
each test component runs hermetically.

To create a new Starnix test component, first add the following include to the
test `.cml`:

```
include: [ "//src/proc/tests/starnix_test.shard.cml" ]
```

This shard sets the `runner` of the component to `starnix_test_runner` and
creates the collection that `starnix_test_runner` will use to instantiate the
test's `starnix_runner` instance.

Once the `.cml` is defined, the test package needs to be updated to include the
appropriate galaxy.

For example, the build template used for running tests in a [bionic][bionic]
based environment:

```
template("starbionic_test_package") {
  starnix_test_package(target_name) {
    forward_variables_from(invoker, "*")

    galaxy = "//src/proc/bin/galaxies/starbionic_test"
  }
}
```

The `galaxy` argument points to the directory containing the galaxy definition.

## Starnix Unit Test Runner

This runner is intended to be used by the starnix runner's unit tests. It is a
wrapper around the regular Rust test runner that adds additional permissions
required by starnix unit tests that shouldn't be available to regular Rust test
components.

[test-runner]: ../README.md
[bionic]: https://android.googlesource.com/platform/bionic/