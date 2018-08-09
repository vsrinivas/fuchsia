# Contributing

## Unsafe

`unsafe` is not allowed, except for in the `boringssl` module. We
`#![deny(unsafe)]`, so code that uses `unsafe` outside of that module should
fail to compile. For details on how to use `unsafe` in the `boringssl` module,
see the doc comment on that module.

## `#[must_use]`

The `#[must_use]` directive causes the compiler to emit a warning if code calls
a function but does not use its return value. It is a very useful lint against
failing to properly act on the result of cryptographic operations. A
`#[must_use]` directive should go on:
- All functions/methods (including in trait definitions) which return a value and are visible outside of the
  crate
- In the `boringssl` module:
  - All functions/methods (including in trait definitions) which return a value and are visible outside of the
    `boringssl` module or are exported from the `raw` or `wrapper` modules to
    the top-level `boringssl` module

`#[must_use]` may also be used on types, but should be evaluated on a
case-by-case basis.