# Fidl

Fidl is the IPC system for Fuchsia.

## Compiler

## Language

### Readability rubric

Fuchsia has adopted a [readability rubric](../../api/fidl.md) for FIDL libraries.

## Bindings

### C

- [Documentation](c.md)
- [Echo server example](https://fuchsia.googlesource.com/garnet/+/master/examples/fidl/echo2_server_c/)

### C++

- [Documentation](cpp.md)
- [Echo server example](https://fuchsia.googlesource.com/garnet/+/master/examples/fidl/echo2_server_cpp/)
- [Echo client example](https://fuchsia.googlesource.com/garnet/+/master/examples/fidl/echo2_client_cpp/)

### Dart

- [Echo server example](https://fuchsia.googlesource.com/topaz/+/master/examples/fidl/echo_server_dart/)
- [Echo client example](https://fuchsia.googlesource.com/topaz/+/master/examples/fidl/echo_client_dart/)

### Go

- [Echo server example](https://fuchsia.googlesource.com/garnet/+/master/examples/fidl/echo2_server_go/)
- [Echo client example](https://fuchsia.googlesource.com/garnet/+/master/examples/fidl/echo2_client_go/)

### Rust

- [Echo server example](https://fuchsia.googlesource.com/garnet/+/master/examples/fidl/echo2_server_rust/)
- [Echo client example](https://fuchsia.googlesource.com/garnet/+/master/examples/fidl/echo2_client_rust/)

## Learning

See the [tutorial](tutorial.md) to learn about Fidl service development.

## FIDL Tuning Proposals

Substantial changes to FIDL (whether the language, the wire format, or
language bindings) are described in [FIDL Tuning Proposals]. These
decisions are recorded here for posterity. This includes both accepted
and rejected designs. [FTP-001] describes the proposal process itself.

[FIDL Tuning Proposals]: ./ftp
[FTP-001]: ./ftp/ftp-001.md
