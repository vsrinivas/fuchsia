# Garnet directory structure

This document describes the directory structure of this repository.

[TOC]

## Common parts

Garnet follows the common aspects of layer repository structure defined in
[Layer repository structure](https://fuchsia.googlesource.com/docs/+/master/layer_repository_structure.md).

This file documents the Garnet-specific pieces.

## public/rust/crates/

The `public/rust/crates/` directory contains crates used by the Rust programming
language. These libraries are separate from `public/lib` to conform with the
norms of the Rust community.

Rust code that is a private implementation detail of this repository should not
be in this directory. Instead, that code should be placed analogously to code
written in other languages.

## drivers/

The `drivers/` directory contains device drivers, which are typically built as
shared libraries that link against the Zircon DDK.

## go/src/

The `go/src/` directory contains code implemented in the Go programming
language. This code is separate from the rest of the code because the Go build
system requires a directory named `src` on the path and uses the directory
structure below the `src` directory to define the namespace for imports.
