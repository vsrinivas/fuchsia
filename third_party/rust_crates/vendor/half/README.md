# `f16` and `bf16` floating point types for Rust
[![Crates.io](https://img.shields.io/crates/v/half.svg)](https://crates.io/crates/half/) [![docs.rs](https://docs.rs/half/badge.svg)](https://docs.rs/half/) [![Build Status](https://travis-ci.org/starkat99/half-rs.svg?branch=master)](https://travis-ci.org/starkat99/half-rs) [![Build status](https://ci.appveyor.com/api/projects/status/bi18aypi3h5r88gs?svg=true)](https://ci.appveyor.com/project/starkat99/half-rs)

This crate implements a half-precision floating point `f16` type for Rust implementing the IEEE 754-2008 standard
[`binary16`](https://en.wikipedia.org/wiki/Half-precision_floating-point_format) a.k.a `half` format,
as well as a `bf16` type implementing the [`bfloat16`](https://en.wikipedia.org/wiki/Bfloat16_floating-point_format) format.

## Usage

The `f16` and `bf16` types provides conversion operations as a normal Rust floating point type, but since they are primarily leveraged for
minimal floating point storage and most major hardware does not implement them, all math operations should be done as an `f32` type.

This crate provides [`no_std`](https://rust-embedded.github.io/book/intro/no-std.html) support by default so can easily be used in embedded
code where a smaller float format is most useful.

*Requries Rust 1.32 or greater.* If you need support for older versions of Rust, use versions 1.3 and earlier of this crate.

See the [crate documentation](https://docs.rs/half/) for more details.

### Optional Features

- **`serde`** - Implement `Serialize` and `Deserialize` traits for `f16` and `bf16`. This adds a dependency on the
[`serde`](https://crates.io/crates/serde) crate.

- **`use-intrinsics`** - Use hardware intrinsics for `f16` and `bf16` conversions if available on the compiler host target. By
default, without this feature, conversions are done only in software, which will be the fallback if the host target does
not have hardware support. **Available only on Rust nightly channel.**

- **`alloc`** - Enable use of the [`alloc`](https://doc.rust-lang.org/alloc/) crate when not using the `std` library.

  This enables the `vec` module, which contains zero-copy conversions for the `Vec` type. This allows fast conversion between
  raw `Vec<u16>` bits and `Vec<f16>` or `Vec<bf16>` arrays, and vice versa. *Requires Rust 1.36 or greater.*

- **`std`** - Enable features that depend on the Rust `std` library, including everything in the `alloc` feature.

  Enabling the `std` feature enables runtime CPU feature detection when the `use-intrsincis` feature is also enabled.
  Without this feature detection, intrinsics are only used when compiler host target supports them.

### More Documentation

- [Crate API Reference](https://docs.rs/half/)
- [Latest Changes](CHANGELOG.md)

## License

This library is distributed under the terms of either of:

* MIT license ([LICENSE-MIT](LICENSE-MIT) or
[http://opensource.org/licenses/MIT](http://opensource.org/licenses/MIT))
* Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0))

at your option.

### Contributing

Unless you explicitly state otherwise, any contribution intentionally submitted for inclusion in the
work by you, as defined in the Apache-2.0 license, shall be dual licensed as above, without any
additional terms or conditions.
