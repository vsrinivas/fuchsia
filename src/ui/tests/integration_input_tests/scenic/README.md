# Integration Input Tests

This collection of tests exercises the input dispatch paths in core components,
such as Scenic and Root Presenter. They are intended to be fairly minimal, free
of flakiness, and standalone - the entire test is in one file.

To run these, first make sure that your machine's display controller is free,
by killing a running Scenic with this command:

```shell
fx shell killall scenic.cmx
```

Then you may run these tests in serial, like so:

```shell
fx test integration_input_tests
```
