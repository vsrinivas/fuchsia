# fidlc

Refer to the [compiler
reference page](https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/compiler#compiler_internals) for
documentation.

## Goldens

The fidlc goldens are the result of running fidlc on the test libraries in
//tools/fidl/fidlc/testdata (see README.md there). In particular, we store
golden files for the JSON IR and coding tables.

To check or update goldens, run `fx check-goldens fidlc`.
