# Dart echo pipelined example

To run:

On host:

```
fx set <product>.<board> --with examples/fidl/dart:all
fx build
fx run fuchsia-pkg://fuchsia.com/example-launcher-dart#meta/example-launcher-dart.cmx fuchsia-pkg://fuchsia.com/echo-launcher-dart-client#meta/echo-launcher-dart-client.cmx fuchsia-pkg://fuchsia.com/echo-launcher-dart-server#meta/echo-launcher-dart-server.cmx fuchsia.examples.EchoLauncher
```
