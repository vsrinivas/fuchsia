# Generated code output guide

This document outlines the approaches available for viewing generated FIDL
bindings code. For general information about how FIDL definitions get converted
to target language code, refer to the [bindings reference][bindings-ref].

## Viewing generated code

If you would like to look at the generated output for a specific target in the
Fuchsia build, you can first build the target in question, then inspect the
generated files in the build output. The instructions in this section assume
that the target is defined using the standard [`fidl` GN template][fidl-gn].

### GN build

The root of the FIDL output in the GN build for a library `fuchsia.examples` defined in
directory `sdk/fidl` is:

    out/default/fidling/gen/sdk/fidl/fuchsia.examples

#### HLCPP, LLCPP, and C {#c-family}

The C family bindings are further generated into a `fuchsia/examples` subdirectory. From
there:

- HLCPP outputs `cpp/fidl.cc`, `cpp/fidl.h`, and `cpp/fidl_test_base.h`.
- LLCPP outputs `llcpp/fidl.cc` and `llcpp/fidl.h`.
- C outputs `c/fidl.client.c`, `c/fidl.server.c`, and `fidl.h`.

For example, using `fuchsia.io` with the HLCPP bindings creates the
following files:

    out/default/fidling/gen/sdk/fidl/fuchsia.io/fuchsia/io/cpp/fidl.cc
    out/default/fidling/gen/sdk/fidl/fuchsia.io/fuchsia/io/cpp/fidl.h
    out/default/fidling/gen/sdk/fidl/fuchsia.io/fuchsia/io/cpp/fidl_test_base.h

and using `fuchsia.io` with the LLCPP bindings creates the following files:

    out/default/fidling/gen/sdk/fidl/fuchsia.io/fuchsia/io/llcpp/fidl.cc
    out/default/fidling/gen/sdk/fidl/fuchsia.io/fuchsia/io/llcpp/fidl.h

#### Rust

The Rust bindings output is contained in a single file in the root directory.
For example, `fuchsia.io` is generated to the following file:

    out/default/fidling/gen/sdk/fidl/fuchsia.io/fidl_fuchsia_io.rs

#### Go

Go bindings are generated into an `impl.go` file located in a subdirectory in
the root directory. For example, building `fuchsia.io` generates the following file:

    out/default/fidling/gen/sdk/fidl/fuchsia.io/fuchsia.io.fidl/impl.go

### JSON IR

The JSON IR is generated to the root directory:

    out/default/fidling/gen/sdk/fidl/fuchsia.io/fuchsia.io.fidl.json

### Dart

## Using fidlbolt

For ad hoc examples or existing FIDL files, another option is to use the
`fidlbolt` tool. By pasting the desired FIDL content into fidlbolt, it is
possible to view the output for each binding, as well as for the JSON IR.
`fidlbolt` also supports importing libraries defined in the sdk, so that
the FIDL code can use e.g. `using fuchsia.io;` to refer to sdk libraries.

## Viewing generated documentation

### Rust

It is possible to generate documentation for generated Rust bindings using
[`fx rustdoc`][rustdoc].

<!-- xrefs -->
[bindings-ref]: /docs/reference/fidl/bindings/overview.md
[fidl-gn]: /build/fidl/fidl.gni
[rustdoc]: /docs/development/languages/rust/fidl_crates.md#documentation
