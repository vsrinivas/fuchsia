# Inspect Rust Codelab Example

Reviewed on: 2020-01-10

This directory contains the example program for the Inspect Rust Codelab.

## Building

To add this project to your build, append `--with
//examples/diagnostics/inspect/codelab/rust` to the `fx set` invocation.

## Running

The example program consists of a client program that starts the codelab
server and connects to it.  The codelab server implements a service called
"Reverser" that simply reverses strings passed to it.

Each part of the codelab has its own component, and the client program
can be configured to open a specific part.

To run Part 2 of the codelab and pass the strings "Hello" and "World"
to the service, run the following:
``
$ fx shell run inspect_rust_codelab_client 2 Hello World
``

## Testing

Unit tests for the codelab are available in the `inspect_rust_codelab_unittests`
package.

Integration tests are also available in the
`inspect_rust_codelab_integration_tests`
package.

``
$ fx run-test inspect_rust_codelab_unittests
$ fx run-test inspect_rust_codelab_integration_tests
``

## Source layout

- `client/` contains the source for `inspect_rust_codelab_client`
- `fizzbuzz/` contains the source for `inspect_rust_codelab_fizzbuzz`,
  a service that the codelab server depends on to demonstrate service
  dependencies.
- `part_#/`, where # is a number, contains the source for the respective
  part of the codelab.
- `part_#/tests` contains the source for the integration tests.
- `inspect-codelab-shared` contains utilities for creating the environment where
  the codelab components run and to launch them.
