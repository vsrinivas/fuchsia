# Integration tests

Integration tests are written against client-facing FIDL services exposed by
Modular. They make use of the Modular Test Harness.

## Writing a new test

The easiest way to get started is to make a copy of an existing test. Be sure to:

1. In `BUILD.gn`, add an entry to build the new test `executable()`
1. Create a .cmx file in `meta/` if the new test needs special capabilities
1. In `BUILD.gn`, create a `fuchsia_unittest_component()` with the new
   `executable()` as a dependency
1. In `BUILD.gn`, add the new `fuchsia_unittest_component()` as a
   `test_component` of the `modular_integration_tests` `fuchsia_test_package()`

## Running tests

Run the following commands to build your tests:

```sh
fx set core.x64 --with //src/modular/tests
fx build
```

NOTE: You only need to run `fx set` once.

### Running all tests

`fx test modular_integration_tests`

### Running one test

Add the following to the above command:

`-- --gtest_filter="{ClassName}.{TestName}"`
