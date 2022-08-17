## Build the test

```shell
$ fx set <product>.<arch> --with //src/ui/tests/integration_input_tests/touch:tests
```

## Run the test

To run the fully-automated test, use this fx invocation:

```shell
$ fx test touch-input-test
```

To see extra logs:

```shell
$ fx test --min-severity-logs=DEBUG touch-input-test -- --verbose=2
```

### Add trace metrics to the test

This test suite uses the category `touch-input-test` to log trace events. Any new categories added
to a test will need to be included in the `fx traceutil record` command below.

Trace event types can be found in
[`libtrace`](//zircon/system/ulib/trace/include/lib/trace/event.h).

### Record a trace of the test

Add the tracing package to your `fx set`:

```shell
$ fx set <product>.<arch> --with //src/ui/tests/integration_input_tests/touch:tests --with-base=//bundles/packages/prod:tracing
```

To record a trace of the test, use this fx invocation:

```shell
$ fx traceutil record --duration 20s --categories touch-input-test fuchsia-pkg://fuchsia.com/touch-input-test#meta/touch-input-test.cm
```

Note: The default duration for `traceutil record` is 10 seconds. When running
locally, package resolving can take more than 10 seconds. If the recording ends
before the test completes, increase the amount of time in the `--duration` flag.

## Play with clients

You can use [`tiles-session`](/src/session/examples/tiles-session/README.md) to manually run and
interact with any of the child clients under test.

To use `tiles-sesion`, first make sure to add `//src/session/examples/tiles-session:packages` to
your gn build args, e.g:

```
fx set <product>.<board> --with //src/session/examples/tiles-session:packages
```

### Play with the Flutter client

To play around with the Flutter client used in the automated test, invoke the client like this:

```shell
$ ffx session add fuchsia-pkg://fuchsia.com/one-flutter#meta/one-flutter.cm
```

### Play with the C++ GFX client

To play around with the C++ GFX client used in the automated test, invoke the client like this:

```shell
$ ffx session add fuchsia-pkg://fuchsia.com/touch-gfx-client#meta/touch-gfx-client.cm
```

### Play with the web client

To play around with the web client used in the automated test, invoke the client like this:

```shell
$ ffx session add fuchsia-pkg://fuchsia.com/one-chromium#meta/one-chromium.cm
```
