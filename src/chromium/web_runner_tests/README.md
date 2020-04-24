## `web_runner_tests`

Contains integration tests to ensure that Chromium is compatible with Fuchsia.

## Build the test

```shell
$ fx set <product>.<arch> --with //src/chromium/web_runner_tests:tests
$ fx build
```

## Run the test

Remember to kill a running Scenic before starting the test. In particular, the pixel tests must be
run on a product without a graphical interface, such as `core`.
(If the zircon console is running, you don't need to do this.)

```shell
$ fx shell killall scenic.cmx
```

To run all the tests, use this fx invocation:

```shell
$ fx test web_runner_tests
```

To run individual test suites, use these fx invocations:

```shell
fx test web_runner_tests -t -- --gtest_filter="WebRunnerIntegrationTest.*"
fx test web_runner_tests -t -- --gtest_filter="WebRunnerPixelTest.*"
fx test web_runner_tests -t -- --gtest_filter="WebPixelTest.*"
```

For more information about the individual tests, see their respective file
comments.