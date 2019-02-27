vte
===

[![Build Status](https://travis-ci.org/jwilm/vte.svg?branch=master)](https://travis-ci.org/jwilm/vte)
[![Crates.io Version](https://img.shields.io/crates/v/vte.svg)](https://crates.io/crates/vte/)

Parser for implementing virtual terminal emulators in Rust.

The parser is implemented according to [Paul Williams' ANSI parser state
machine]. The state machine doesn't assign meaning to the parsed data and is
thus not itself sufficient for writing a terminal emulator. Instead, it is
expected that an implementation of the `Perform` trait which does something
useful with the parsed data. The `Parser` handles the book keeping, and the
`Perform` gets to simply handle actions.

See the [docs] for more info.

## Developer Notes

If contributing to either `vte` or the `utf8parse` crate and modifying a
_table.rs.in_ file, make sure to `cargo run` from the _codegen_ folder so that
the compiled tables are updated.

[Paul Williams' ANSI parser state machine]: https://vt100.net/emu/dec_ansi_parser
[docs]: https://docs.rs/crate/vte/
