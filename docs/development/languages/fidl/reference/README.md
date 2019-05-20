# Introduction

The reference section provides the following material:

* [Attributes](attributes.md) &mdash; describes the available FIDL attributes
* [Compiler](compiler.md) &mdash; describes the organization of the compiler
* [Editors](editors.md) &mdash; discusses support for FIDL in IDEs and stand-alone editors
* [FIDL Tuning Proposals](ftp/README.md) &mdash; accepted and rejected changes for FIDL
* [Grammar](grammar.md) &mdash; the FIDL grammar
* [`library zx`](library-zx.md) &mdash; the Zircon system library
* [JSON IR](json-ir.md) &mdash; a tour of the JSON Intermediate Representation (**JSON IR**) generator
* [Language](language.md) &mdash; defines the syntax of the FIDL language
* [Wire Format](wire-format/README.md) &mdash; details the byte-by-byte organization of data
* [FIDL ABI and Source Compatibility Guide](abi-compat.md) &mdash; how to evolve FIDL APIs
* [Host](host.md) &mdash; summary of the parts of FIDL that are allowed on host

### Readability rubric

Fuchsia has adopted a [readability rubric](../../../api/fidl.md) for FIDL libraries.

## Bindings

* [Specification](bindings.md)

### C

- [Documentation](../languages/c.md)
- [Echo server example](/garnet/examples/fidl/echo_server_c/)

### C++

- [Documentation](../languages/cpp.md)
- [Echo server example](/garnet/examples/fidl/echo_server_cpp/)
- [Echo client example](/garnet/examples/fidl/echo_client_cpp/)

### Dart

- [Documentation](../tutorial/tutorial-dart.md)
- [Echo server example](https://fuchsia.googlesource.com/topaz/+/master/examples/fidl/echo_server_async_dart/)
- [Echo client example](https://fuchsia.googlesource.com/topaz/+/master/examples/fidl/echo_client_async_dart/)

### Go

- [Documentation](../tutorial/tutorial-go.md)
- [Echo server example](/garnet/examples/fidl/echo_server_go/)
- [Echo client example](/garnet/examples/fidl/echo_client_go/)

### Rust

- [Documentation](../tutorial/tutorial-rust.md)
- [Echo server example](/garnet/examples/fidl/echo_server_rust/)
- [Echo client example](/garnet/examples/fidl/echo_client_rust/)

## Learning

See the [tutorial](../tutorial/README.md) to learn about FIDL service development.

FIDL Plugins exist for multiple editors and IDEs.  See the
[editor page](editors.md) to learn more.

## FIDL Tuning Proposals

Substantial changes to FIDL (whether the language, the wire format, or
language bindings) are described in [FIDL Tuning Proposals]. These
decisions are recorded here for posterity. This includes both accepted
and rejected designs. [FTP-001] describes the proposal process itself.
Use the [template](ftp/template.md) when starting a new proposal.

[FIDL Tuning Proposals]: ftp/README.md
[FTP-001]: ftp/ftp-001.md
