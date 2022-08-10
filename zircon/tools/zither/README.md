# zither

`zither` is a FIDL backend for generating simple language bindings for
'data layouts', by which we mean bindings representing memory formats (e.g.,
syscall or ZBI data types) that mirror the format proper (either directly
with a C-style representation or with a small, provided transformation to get
there). zither in turn has its own (sub)backends (see below).

## Supported backends

Here we document the supported zither backends and their particularities.
Consider a FIDL library by the name of `${id1}.${id2}.....${idn}`:

### C
* A single header `<${id1}/${id2}/.../${idn}/${idn}.h>` is generated.
  - TODO(fxbug.dev/51002): this is a simple placeholder policy.
* Type translation from FIDL to C is as follows:
  - `{u,}int{8,16,32,64}`map to `{u,}int{8,16,32,64}_t`, respectively;
  - `bool` maps to `bool`;
  - unbounded `string`s are only permitted in constants and map to string
  literals;
  - TODO(fxbug.dev/51002): Document more as we go.
* A constant declaration yields a preprocessor variable of name
`UpperSnakeCase(${id1}_${id2}_..._${idn}_${declname})`;
* TODO(fxbug.dev/51002): Document more as we go.

TODO(fxbug.dev/91102): Also C++, Rust, and Go.

TODO(fxbug.dev/93393): Also Assembly.

## Testing
zither's testing strategy is three-fold.

### Build-time golden tests
Executing golden tests at build-time is significantly more convenient than at
runtime: ninja executes them with maximal parallelism, testing is a
side-effect of building and not something that requires shell script glue
around complex packaging of golden and generated files (which will quickly
grow to be quite numerous), and the updating of goldens will amount to running
a build-generated `cp` command (as opposed to something involving more
cross-referencing and copy-pasting). See `zither_golden_test()` in `BUILD.gn`
for more detail.

It is worth noting that this form of testing should not increase build times in
any meaningful way: whether diffing happens at build-time or runtime, the build
would dedicate the same amount of time to `fidlc` and `zither` invocations - and,
further, build-time diffing is imperceptibly quick.

### Compilation integration tests
Testing that code was emitted as expected can only be part of the strategy; one
needs to further verify that the generated code is actually valid and compiles.
This is covered by another form of build-time testing that aims to do exactly
that for each of the supported backends.

### Basic unit-testing
Finally, pieces of the generation logic of sufficient modularity and complexity
will be conventionally unit-tested. This in particular includes the translation
from FIDL's IR to another of zither's.
