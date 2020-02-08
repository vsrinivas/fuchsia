# session_manager

Reviewed on: 2020-02-04
These are integration tests for [session_manager](//src/session/bin/session_manager).

## Add Integration Tests to Build

These tests can be added to build by adding `--with //src/session/tests` to your existing `fx
set` command so that it looks like:
```
fx set <PRODUCT>.<BOARD> --with-base=//src/session,//src/session/bin/session_manager:session_manager.config --with //src/session:tests
```
To see a list of possible products, run: `fx list-products`

To see a list of possible boards, run: `fx list-boards`

## Run Tests

`session_manager` integration tests are available in the `session_manager_integration_tests`
package.
```
$ fx run-test session_manager_integration_tests
```

## Source layout

The entry point for the integration tests is located in `src/main.rs`.
