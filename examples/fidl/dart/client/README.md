# Dart echo client example

To run:

On host:

```
fx set <product>.<board> --with topaz/examples/fidl:all
fx build
fx run fuchsia-pkg://fuchsia.com/example-launcher-dart#meta/example-launcher-dart.cmx fuchsia-pkg://fuchsia.com/echo-dart-client#meta/echo-dart-client.cmx fuchsia-pkg://fuchsia.com/echo-dart-server#meta/echo-dart-server.cmx fuchsia.examples.Echo
```
