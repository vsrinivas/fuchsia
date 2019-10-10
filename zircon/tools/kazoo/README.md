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

- All .fidl files must be part of `library zz` or `library zx`. "zx" is currently used by
  FIDL-proper to magically define the existing defintions of some basic types, e.g. `zx.status` that
  are used by other FIDL files. Because of this "zz" is used for the new syscall definitions. In the
  future, Kazoo will accept only `library zx`, and the automatically generated "zx" and the syscalls
  in "zz" will be merged into "zx" that will all appear in source `.fidl` files.

- Type aliases used to impart meaning. `alias_workarounds.fidl` includes various aliases that expand
  to something similar to the correct type, however Kazoo treats these specially. For example,
  `mutable_string` is used to indicate that the type is a string, but that should be treated as
  mutable (generally for a string that's both input and output).

- Doc comments of the form `/// Rights: ...` are used by the Kazoo JSON generator, and are
  propagated to the documentation update script.

- Attributes of the form `[Vdsocall]`, `[Const]`, etc. correspond to the previous similar
  definitions in abigen.

- Some structs are defined in the current `.fidl` files, however, they're not used to generate the
  real Zircon headers yet. Similarly for enums, bits, etc. Only `protocol`s are used to define the
  function syscall interface.

- There's a FIDL CamelCase to Zircon name mapping which happens. As an example: a protocol named
  `Object`, containing a method `WaitMany` will turn into `zx_object_wait_many()` in kernel-land.

## Testing

There are unittests in `kazoo-test` which are in the source tree next to the rest of the
implementation.

Additionally, during development of Kazoo, the current `syscalls.banjo` (which generates
`syscalls.abigen`) is maintained. `zircon/tools/kazoo/difftest` runs `banjo` and `abigen` on the
current syscall definitions, and runs `fidlc` and `kazoo` on the new syscall definitions (currently
in `//zircon/experimental/syscalls`) and runs a `diff` to confirm that the output from both paths is
identical.

It should be run with a cwd of `//zircon/tools/kazoo` as `./difftest`. There's some additional
instructions in the script.
