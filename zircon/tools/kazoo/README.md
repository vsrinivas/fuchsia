# Kazoo: Backend for fidlc for syscall definitions

## Summary

`kazoo` is a host-side tool that operates as a backend for `fidlc`. In particular, Kazoo is used
to process the definitions of kernel syscalls, and output various formats used by the kernel, user
space, and other tools. See `kazoo -h` for a complete list of the output formats.

`fidlc` is run first to parse and interpret `.fidl` files, and it outputs a JSON IR representation.
`kazoo` then processes the JSON IR.

## FIDL syntax

In order to use the base FIDL syntax to express syscalls, some experimental syntax and style
extensions are used. A possibly-incomplete outline of these as compared with standard FIDL syntax
includes:

- The attribute `[Transport="Syscall"]` must be applied to all protocols that are part of the
  syscall interface.

- All .fidl files must be part of `library zx` (for syscalls) or `library zxio`.

- Type aliases used to impart meaning. `alias_workarounds.fidl` includes various aliases that expand
  to something similar to the correct type, however Kazoo treats these specially. For example,
  `MutableString` is used to indicate that the type is a string, but that should be treated as
  mutable (generally for a string that's both input and output).

- Doc comments of the form `/// Rights: ...` are used by the Kazoo JSON generator, and are
  propagated to the documentation update script.

- Attributes of the form `[vdsocall]`, `[const]`, etc. correspond to the previous similar
  definitions in abigen.

- Some structs are defined in the current `.fidl` files, however, they're not used to generate the
  real Zircon headers yet. Similarly for enums, bits, etc. Only `protocol`s are used to define the
  function syscall interface.

## Testing

There are unittests in `kazoo-test` which are in the source tree next to the rest of the
implementation.

To run these tests, use `--with-host=//zircon/tools/kazoo:tests` with your `fx set` command, and
then use `fx test` to run the tests, e.g.:

```
$ fx set core.x64 --with-host=//zircon/tools/kazoo:tests
$ fx test //zircon/tools/kazoo
```

This also includes a "golden"-style run, which compares the output of running kazoo on any current
syscalls changes with `//zircon/tools/kazoo/golden.txt`. Run `fx build` with the `fx set` line
above to see instructions on how to update `golden.txt` if output differs.
