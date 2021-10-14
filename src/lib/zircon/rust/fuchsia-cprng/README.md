Rust bindings for Zircon kernel CPRNG
=====================================

This repository contains Rust language bindings for Zircon kernel CPRG. It is
intended to be a minimal dependency for out-of-tree callers. In-tree callers
should use `fuchsia_zircon` instead.

There are two ways to build Rust artifacts targeting Fuchsia; using the
[Fargo](https://fuchsia.googlesource.com/fargo/) cross compiling tool or
including your [artifact in the GN
build](https://fuchsia.dev/fuchsia-src/development/languages/rust). Of the two,
Fargo is likely better for exploration and experimentation.
