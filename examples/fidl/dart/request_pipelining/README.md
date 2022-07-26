# Dart echo pipelined example

To run:

On host:

```shell
$ fx set <product>.<board> --with examples/fidl/dart:all \
    --with-base //src/dart \
    --args='core_realm_shards += [ "//src/dart:dart_runner_core_shard" ]'
$ fx build
$ ffx component run fuchsia-pkg://fuchsia.com/echo-launcher-dart#meta/echo_realm.cm
$ ffx component start /core/ffx-laboratory:echo_realm/echo_client
```
