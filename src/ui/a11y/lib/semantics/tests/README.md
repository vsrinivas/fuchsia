# `semantics manager tests`

## Running the tests

To run all of the semantics tests, build with a configuration such as
```
fx set workstation.<board> --with //src/ui/a11y/bundles:tests
```

And run the tests using
```
# Run the integration tests
ffx test run fuchsia-pkg://fuchsia.com/flutter-semantics-test#meta/flutter-semantics-test-component.cm
ffx test run fuchsia-pkg://fuchsia.com/web-semantics-test#meta/web-semantics-test-component.cm
# Run the unit tests
ffx test run fuchsia-pkg://fuchsia.com/a11y_lib_tests#meta/semantics_tests.cm
```

## Integration test internals

The semantics integration tests launch private instances of scenic and web\_runner or
flutter\_runner (depending on whether the web tests or the flutter tests are running).
The tests provide a fuchsia.accessibility.semantics.SemanticsManager, which is used to inspect
semantic data sent by the runner under test and to trigger semantic actions in runner.

### Web semantic integration tests

These tests launch a web\_runner and load html declared in web_semantics_tests.cc.

### Flutter semantic integration tests

These tests launch a flutter\_runner running the [a11y-demo](/src/ui/a11y/bin/demo) app.
