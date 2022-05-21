# `semantics manager tests`

## Running the tests

To run all of the semantics tests, build with a configuration such as
```
fx set workstation.<board> --with //src/ui/a11y/bundles:tests
```

And run the tests using
```
# Run the integration tests
ffx test run fuchsia-pkg://fuchsia.com/semantics-integration-tests#meta/flutter_semantics_tests.cm
ffx test run fuchsia-pkg://fuchsia.com/semantics-integration-tests#meta/web_semantics_tests.cm
# Run the unit tests
ffx test run fuchsia-pkg://fuchsia.com/a11y_lib_tests#meta/semantics_tests.cm
```

## Integration test internals

The semantics integration tests launch private instances of scenic and web\_runner or
flutter\_runner (depending on whether the web tests or the flutter tests are running).
The tests provide a fuchsia.accessibility.semantics.SemanticsManager, which is used to inspect
semantic data sent by the runner under test and to trigger semantic actions in runner.

### Web semantic integration tests

These tests launch a web\_runner and load html pages from the test package.  These test pages are
sourced from the [testdata](/src/ui/a11y/lib/semantics/tests/testdata) directory.

### Flutter semantic integration tests

These tests launch a flutter\_runner running the [a11y-demo](/src/ui/a11y/bin/demo) app.
