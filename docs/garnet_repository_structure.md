# Garnet directory structure

This document describes the directory structure of this repository.

[TOC]

This directory structure is likely to evolve over time. For example, we might
add more topic groups within `public/lib` and `bin` as the number of
subdirectories increases.

## public/

The `public/` directory defines the public interface to this repository. Code
outside this repository should not depend on files outside of this directory.
This directory should not depend on any files in this repository outside of this
directory. This property ensures that code outside this repository does not
transitively depend on any of the private files in this repository.

### public/build/

The `public/build/` directory contains files that support the build systems of
clients of the `public/` directory.

### public/lib/

The `public/lib/` directory contains libraries (both static and dynamic) that
clients can link into their processes. Many of the libraries in this directory
are defined in FIDL, which are language-agnostic definitions of interprocess
communication protocols. However, this directory also contains manually
implemented libraries in various languages. In these cases, both the headers and
source files for these libraries are included in this directory.

Libraries that are private implementation details of this repository (i.e., not
part of this repository's public interface) should be in `lib/` instead.

### public/dart-pkg/

The `public/dart-pkg/` directory contains Dart packages that do not have
corresponding libraries in other languages. For example, this directory contains
the packages that define the Fuchsia-specific interface between the Dart code
and the Dart runtime.

Dart packages that have corresponding libraries in other languages should be in
`public/lib` along side the implementations in those other languages.

### public/rust/crates/

The `public/rust/crates/` directory contains crates used by the Rust programming
language. These libraries are separate from `public/lib` to conform with the
norms of the Rust community.

Rust code that is a private implementation detail of this repository should not
be in this directory. Instead, that code should be placed analogously to code
written in other languages.

## bin/

The `bin/` directory contains executable binaries. Typically, these binaries
implement one or more of the interfaces defined in `public/`, but the binaries
themselves are not part of the public interface of this repository.

## docs/

The `docs/` directory contains documentation about this repository.

## drivers/

The `drivers/` directory contains device drivers, which are typically built as
shared libraries that link against the Zircon DDK.

## examples/

The `examples/` directory contains code that demonstrates how to use the public
interfaces exposed by this repository. This code should not be required for the
system to execute correctly and should not depend on the private code in this
repository.

## lib/

The `lib/` directory contains libraries (both static and dynamic) that are used
internally by this repository. These libraries are internal implementation
details of this repository and should not be used by code outside this
repository.

Libraries that are part of the public interface of this repository (i.e., not
private implementation details) should be in `public/lib/` instead.

## go/src/

The `go/src/` directory contains code implemented in the Go programming
language. This code is separate from the rest of the code because the Go build
system requires a directory named `src` on the path and uses the directory
structure below the `src` directory to define the namespace for imports.
