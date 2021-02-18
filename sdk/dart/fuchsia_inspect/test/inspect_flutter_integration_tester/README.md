# Inspect integration tester

A module that runs integration tests on the Dart Inspect library.

## Running the test

Assumes you already have an existing Fuchsia checkout and have set-up your
 hardware.

This will briefly launch the mod, push some buttons via flutter driver, then
 exit.

1.  Run "core" so the test driver can set up its own UI.

    ```
    fx set core.chromebook-x64 --with '//topaz/bundles:buildbot' \
    --with //bundles:kitchen_sink \
    --with //topaz/public/dart/fuchsia_inspect/test/inspect_flutter_integration_tester:inspect_dart_integration_test_driver \
    --with //topaz/public/dart/fuchsia_inspect/test/inspect_flutter_integration_tester:inspect_flutter_integration_tester

    ```

1.  Do a fresh build.

    ```
    fx build
    ```

1.  Connect and turn on the desired device. Serve to hardware.

    ```
    fx serve
    ```

1.  Run the test
    ```
    fx run-test inspect_dart_integration_test_driver
    ```
