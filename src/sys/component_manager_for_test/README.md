# Component Manager for tests

Reviewed on: 2019-07-22

This project contains a temporary prototype implementation of component manager which will be
used by the Test Runner Framework to launch v2 test components until we are able to use
component manager which is started during boot.

## Building

To add this project to your build, append `--with //src/sys/component_manager_for_test` to the `fx
set` invocation.

## Running

```
$ fx shell run component_manager_for_test \<v2_test_component_url\>
```

## Testing

Tests for this project are available in the `tests` folder.

```
$ fx run-test component_manager_for_test_integration_test
```

## Source layout

The entrypoint is located in `src/main.rs`. Integration tests
live in `tests/`.
