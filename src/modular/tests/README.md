# Integration tests

Integration tests are written against client-facing FIDL services exposed by
Modular. They make use of the Modular Test Harness.

## Writing a new test

The easiest way to get started is to make a copy of an existing test. Be sure to:

1. Add an entry in `BUILD.gn` to build the new test `executable()`
1. Create a .cmx meta file in `meta/`
1. Add both the executable binary and the meta file to the `package()` declaration in `BUILD.gn`

## Running tests

Run the following commands to build & run your tests:

```sh
fx set core.x64 --with //src/modular/tests
fx build
```

NOTE: You only need to run `fx set` once.

### Running all tests

`fx shell "run-test-component modular_integration_tests"`
