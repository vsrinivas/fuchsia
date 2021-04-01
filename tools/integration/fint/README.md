# fint

`fint` provides a common high-level interface for continuous integration
infrastructure and local developer tools to build Fuchsia.

## Library

The `fint` Go library exports two main commands:

1. `fint.Set()`, which runs `gn gen`.
1. `fint.Build()`, which runs `ninja` against a build directory that has
   already been configured by `fint.Set()`.

## CLI

The `cmd/fint` directory contains the code for the `fint` tool, which is a thin
wrapper around the `fint` library. It has two commands, `set` and `build`, each
of which calls into the corresponding `fint` library function.

This code is nested under `cmd/fint` within the main `fint` directory so that
`go build` will output an executable named `fint` by default. If it were
directly within the `cmd` directory, then the default executable name would
confusingly be `cmd`.
