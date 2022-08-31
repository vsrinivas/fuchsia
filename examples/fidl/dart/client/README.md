# Dart echo client example

To run:

On host:

```
$ fx set <product>.<board> --with examples/fidl/dart:all \
    --with-base //src/dart \
    --args='core_realm_shards += [ "//src/dart:dart_runner_core_shard" ]'
$ fx build
$ ffx component run /core/ffx-laboratory:echo_realm fuchsia-pkg://fuchsia.com/echo-dart-client#meta/echo_realm.cm
$ ffx component start /core/ffx-laboratory:echo_realm/echo_client
```
