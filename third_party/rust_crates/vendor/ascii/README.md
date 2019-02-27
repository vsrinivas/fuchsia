# ascii

A library that provides ASCII-only string and character types, equivalent to the
`char`, `str` and `String` types in the standard library.

Types and conversion traits are described in the
[Documentation](https://tomprogrammer.github.io/rust-ascii/ascii/index.html).

You can include this crate in your cargo project by adding it to the
dependencies section in `Cargo.toml`:
```toml
[dependencies]
ascii = "0.8"
```

# Using ascii without libstd

Most of `AsciiChar` and `AsciiStr` can be used without `std` by disabling the
default features. The owned string type `AsciiString` and the conversion trait
`IntoAsciiString` as well as all methods referring to these types are
unavailable. The `Error` trait is also unavailable, but `description()` is made
available as an inherent method for `ToAsciiCharError` and `AsAsciiStrError`.

To use the `ascii` crate in `core`-only mode in your cargo project just add the
following dependency declaration in `Cargo.toml`:
```toml
[dependencies]
ascii = { version = "0.8", default-features = false }
```

# Requirements

The minimum supported Rust version is 1.9.0.
Enabling the quickcheck integration requires Rust 1.12.0.

# History

This package included the Ascii types that were removed from the Rust standard
library by the 2014-12 [reform of the `std::ascii` module](https://github.com/rust-lang/rfcs/pull/486).
The API changed significantly since then.

# License

Licensed under either of

 * Apache License, Version 2.0, ([LICENSE-APACHE](LICENSE-APACHE) or http://www.apache.org/licenses/LICENSE-2.0)
 * MIT license ([LICENSE-MIT](LICENSE-MIT) or http://opensource.org/licenses/MIT)

at your option.

## Contribution

Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in the work by you, as defined in the Apache-2.0 license, shall be dual licensed as above, without any
additional terms or conditions.
