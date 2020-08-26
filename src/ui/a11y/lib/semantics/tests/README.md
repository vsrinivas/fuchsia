# `semantics manager tests`

## Running the tests

The semantics integration tests must be run on a product without a graphical base shell,
such as `core` because it starts and stops an instance of Scenic with access to the real
Vulkan loader service.

To run all of the semantics tests, build with a configuration such as
```
fx set core.<board> --with //src/ui/a11y/bundles:tests --with-base //topaz/bundles:buildbot
```

And run the tests using
```
# Run the integration tests
fx test fuchsia-pkg://fuchsia.com/semantics-integration-tests#meta/flutter_semantics_tests.cmx
fx test fuchsia-pkg://fuchsia.com/semantics-integration-tests#meta/web_semantics_tests.cmx
# Run the unit tests
fx test fuchsia-pkg://fuchsia.com/a11y_lib_tests#meta/semantics_tests.cmx
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
