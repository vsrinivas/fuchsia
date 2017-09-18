# Test Runner Configuration

This directory contains configuration files for running tests on continuous
builds and commit queues for Fuchsia.

[Fuchsia Continuous Integration Configuration](https://fuchsia.googlesource.com/infra/config/)
explains how to configure jobs that use these configuration files. Basically,
the standard Fuchsia recipes takes an optional `tests` property, which is the
path to a JSON file relative to Fuchsia root. If `tests` is specified, the
recipe will read the file execute the tests specified in it. The specification
for the JSON file is
[documented here](https://fuchsia.googlesource.com/test_runner/#Test-Config-Description).

## Adding tests to Fuchsia primary

There are a set of tests that we currently run on the primary Fuchsia builds,
which show up on the
[Fuchsia dashboard](https://fuchsia-dashboard.appspot.com/), are maintained by
the Fuchsia build cops, and will eventually run on the commit queue for every
Fuchsia repo.

If you want your tests to run on the Fuchsia primary builds, add them to
[fuchsia.json](./fuchsia.json). You also need to make sure the test results are
correctly reported to the Test Runner service, which is what invokes the tests
and reports their results over TCP to the host machine.

### GoogleTest

If you have a GoogleTest (gtest) binary, include
`//garnet/public/lib/test_runner/cpp:gtest_main` as a dependency of your executable test
target in your `BUILD.gn` file. It provides a `main()` function that runs all
tests and reports success or failure to Test Runner.

### Custom test frameworks

If you are using a customized test framework, you can report the results
directly, using the
[FIDL service](https://fuchsia.googlesource.com/test_runner/+/master/services/test_runner.fidl).
You need to make the following calls in the course of execution:

* Call `Identify()` at start-up.
* Call `Failed()` when any of the tests fail.
* Call `Teardown()` to signal that the tests have finished running.

See
[gtest_reporter.cc](https://fuchsia.googlesource.com/test_runner/+/master/lib/gtest_reporter.cc)
for an example.

### Arbitrary binaries

If you have any other binary that gives a non-zero exit code when it fails, you
can wrap it in the `exec` field in the JSON file with
`/system/bin/report_result`. This executes a given command, checks the exit
code, and relays the pass/fail information to Test Runner. Here's an example:

```json
{
  "name": "go_os_test",
  "exec": "/system/bin/report_result /system/test/go_os_test"
}
```
