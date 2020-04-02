## Build the test

```shell
$ fx set <product>.<arch> --with //src/ui/tests/e2e_input_tests/touch:tests
```

## Run the test

Remember to kill a running Scenic before starting the test.
(If the zircon console is running, you don't need to do this.)

```shell
$ fx shell killall scenic.cmx
```


To run the fully-automated test, use this fx invocation:

```shell
$ fx test touch-input-test -- --gtest_repeat=10
```

## Play with the flutter client

To play around with the flutter client used in the automated test, invoke the client like this:

```shell
$ present_view fuchsia-pkg://fuchsia.com/one-flutter#meta/one-flutter.cmx
```

