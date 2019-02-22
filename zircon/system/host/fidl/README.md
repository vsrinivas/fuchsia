# FIDL compiler

## Pipeline

### Loading a file

All the FIDL files in a library compilation are loaded by a
[SourceManager](include/fidl/source_manager.h). This thing's job is to own the
buffers backing files. These buffers are kept alive for the entire pipeline.
Tokens, for example, are essentially a string view plus some metadata describing
their source location (a file and position).

### Parsing a file

The FIDL compiler first parses each file into an in-memory AST, which is defined
by the structures in [ast.h](include/fidl/ast.h). This parsing operation starts
by reading the file into memory, and then lexing the contents into a
[token](include/fidl/token.h) stream. The [parser](lib/parser.cpp) proper then
parses the stream into the hierarchical AST. At this point names of types are
unresolved (they could end up pointing to types in another file or library, or
simply be garbage), and nested declarations are still nested in the AST.

This step will fail if any of the given files is not valid FIDL.

### Flattening a library

Once all the files are parsed into AST nodes, it's time to flatten the
representation.

Recall that some declarations can be nested. For instance, a const declaration
can be present in a protocol or struct declaration.

Flattening pulls all the declarations out to one level, which entails computing
fully qualified names for nested types.

### Resolving names in a library

Many parts of a FIDL file refer to each other by name. For instance, a struct
may have a field whose type is given by the (possibly qualified) name of some
other struct. Any name that can't be resolved (because it is not present in any
of the given files or library dependencies) causes compilation to fail at this
stage.

### Computing layout

At this stage layouts of all data structures are computed. This includes both
the coding tables for all of the messages defined by the library, as well as the
wire formats of those messages. The in-memory representation of this layout is
defined by the structures in [coded_ast.h](include/fidl/coded_ast.h).

This step can fail in a few ways. If a given message statically exceeds the
limits of a channel message, compilation will fail. Statically exceeding the
recursion limit of FIDL decoding will also cause compilation to fail.

### Backend generation

At this stage, nothing about the FIDL library per se should cause compilation to
fail (anything particular to a certain language binding could fail, or the
compiler could be given a bogus location to put its output etc.).

#### C Bindings

C bindings are directly generated from the FIDL compiler.

#### Everything except C (C++, Rust, Dart, Go, etc)

The FIDL compiler emits a [JSON intermediate representation (IR)](schema.json).
The JSON IR is consumed by an out-of-tree program, named the **back-end**, that
generates the language bindings from the JSON IR.

The officially supported FIDL language back-ends are:

* C++, Rust, and Go:
  [fidlgen](https://fuchsia.googlesource.com/fuchsia/+/master/garnet/go/src/fidl/compiler/backend)
* Dart:
  [fidlgen_dart](https://fuchsia.googlesource.com/topaz/+/master/bin/fidlgen_dart)
