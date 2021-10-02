# `session_manager` integration tests

Reviewed on: 2021-09-22

These are integration tests for [session_manager](//src/session/bin/session_manager).

## Add Integration Tests to Build

These tests can be added to build by adding `--with //src/session/tests` to your existing `fx
set` command:

```
fx set <PRODUCT>.<BOARD> --with //src/session --with //src/session:tests
```

## Run Tests

`session-manager` integration tests are available in the
`session-manager-integration-tests`
package.

```
fx test session-manager-integration-tests
```

## Source layout

The entry point for the integration tests is located in `src/main.rs`.
