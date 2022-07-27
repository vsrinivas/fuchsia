# Dart FIDL Bindings Tests

To run tests:

```shell
$ fx set core.x64 --with //src/tests/fidl/dart_bindings_test:tests
$ fx test dart-bindings-test
```

To see stack traces from test failures, look at the `fx qemu` or `fx log`
output. They do not show up in the `fx test` output.
