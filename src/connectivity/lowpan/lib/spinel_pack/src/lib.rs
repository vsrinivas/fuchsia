// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This Rust crate allows you to easily and safely serialize/deserialize
//! data structures into the byte format specified in [section 3][1]
//! of the [Spinel Protocol Documentation][2]. This documentation assumes
//! some familiarity with that documentation. The behavior of this crate was
//! inspired by the [`spinel_datatype_pack`/`spinel_datatype_unpack`][3] methods
//! from [OpenThread][4].
//!
//! [1]: https://tools.ietf.org/html/draft-rquattle-spinel-unified-00#section-3
//! [2]: https://tools.ietf.org/html/draft-rquattle-spinel-unified-00
//! [3]: https://github.com/openthread/openthread/blob/5b622e690e6dd441498b57e4cafe034937f43629/src/lib/spinel/spinel.h#L4042..L4111
//! [4]: https://github.com/openthread/openthread/
//!
//! # Key Features
//!
//! * Ability to deserialize to borrowed types (like `&str`) in addition to owned types.
//! * Convenient attribute macro (`#[spinel_packed("...")]`) for automatically implementing
//!   pack/unpack traits on datatypes, including structs with borrowed references and nested
//!   Spinel data types.
//! * Convenient in-line macros (`spinel_read!(...)`/`spinel_write!(...)`) for parsing arbitrary
//!   Spinel data immediately in place without the need to define new types.
//! * All macros check the format strings against the types being serialized/deserialized at
//!   compile-time, generating compiler errors whenever there is a mismatch.
//!
//! # Packing/Serialization
//!
//! Serialization is performed via two traits:
//!
//! * [`TryPack`] for serializing to the natural Spinel byte format
//!   for the given type. The key method on the trait is `try_pack()`.
//! * [`TryPackAs<MarkerType>`][TryPackAs] for serializing to one of the specific
//!   Spinel format types. Since this trait is generic, a given type can
//!   have multiple implementations of this trait for serializing to the
//!   byte format specified by `MarkerType`. The key method on the trait
//!   is `try_pack_as()`.
//!
//! The `TryPack`/`TryPackAs` traits operate on types that implement `std::io::Write`,
//! which includes `Vec<u8>` and `&mut&mut[u8]`.
//!
//! # Unpacking/Deserialization
//!
//! Deserialization is performed via three traits:
//!
//! * [`TryUnpack`] for deserializing bytes into an instance of the
//!   associated type `Self::Unpacked` (which is usually `Self`), inferring
//!   the byte format from the type.
//!   This trait supports deserializing to both borrowed types (like `&str`)
//!   and owned types (like `String`), and thus has a lifetime parameter.
//!   The key method on the trait is `try_unpack()`.
//! * [`TryUnpackAs<MarkerType>`][TryUnpackAs] for deserializing a spinel
//!   field that is encoded as a `MarkerType` into an instance of the
//!   associated type `Self::Unpacked` (which is usually `Self`).
//!   This trait supports deserializing to both borrowed types (like `&str`)
//!   and owned types (like `String`), and thus has a lifetime parameter.
//!   The key method on the trait is `try_unpack_as()`.
//! * [`TryOwnedUnpack`], which is similar to [`TryUnpack`] except that
//!   it supports *owned* types only (like `String`). The key method on the
//!   trait is `try_owned_unpack()`.
//!
//! The `TryUnpack`/`TryOwnedUnpack` traits operate on byte slices (`&[u8]`) and
//! byte slice iterators (`std::slice::Iter<u8>`).
//!
//! # Supported Primitive Types
//!
//! This crate supports the following raw Spinel types:
//!
//! Char | Name        | Type              | Marker Type (if different)
//! -----|-------------|-------------------|-------------------
//! `.`  | VOID        | `()`
//! `b`  | BOOL        | `bool`
//! `C`  | UINT8       | `u8`
//! `c`  | INT8        | `i8`
//! `S`  | UINT16      | `u16`
//! `s`  | INT16       | `i16`
//! `L`  | UINT32      | `u32`
//! `l`  | INT32       | `i32`
//! `i`  | UINT_PACKED | `u32`             | [`SpinelUint`]
//! `6`  | IPv6ADDR    | `std::net::Ipv6Addr`
//! `E`  | EUI64       | [`EUI64`]
//! `e`  | EUI48       | [`EUI48`]
//! `D`  | DATA        | `&[u8]`/`Vec<u8>` | `[u8]`
//! `d`  | DATA_WLEN   | `&[u8]`/`Vec<u8>` | [`SpinelDataWlen`]
//! `U`  | UTF8        | `&str`/`String`   | `str`
//!
//! The Spinel *struct* (`t(...)`) and *array* (`A(...)`) types are not
//! directly supported and will result in a compiler error if used
//! in a Spinel format string. However, you can emulate the behavior of
//! spinel structs by replacing the `t(...)` with a `d` and using another
//! Spinel datatype (like one created with the `spinel_packed` attribute)
//! instead of `&[u8]` or `Vec<u8>`.
//!
//! # How Format Strings Work
//!
//! The macro `spinel_write!(...)` can be used to directly unpack Spinel-encoded
//! data into individual fields, with the encoded type being defined by a
//! *format string*. This macro will parse the *format string*, associating each
//! character in the string with a specific *Marker Type* and field argument. For each
//! of the fields in the format, the macro will make a call into
//! `TryPackAs<MarkerType>::try_pack_as(...)` for the given field's argument.
//! If there is no implementation of `TryPackAs<MarkerType>` for the type of
//! the field's argument, a compile-time error is generated.
//!
//! # Examples
//!
//! Struct example:
//!
//! ```
//! use spinel_pack::prelude::*;
//!
//! #[spinel_packed("CiiLUE")]
//! #[derive(Debug, Eq, PartialEq)]
//! pub struct SomePackedData<'a> {
//!     foo: u8,
//!     bar: u32,
//!     blah: u32,
//!     bleh: u32,
//!     name: &'a str,
//!     addr: spinel_pack::EUI64,
//! }
//! ```
//!
//! Packing into a new `Vec`:
//!
//! ```
//! # use spinel_pack::prelude::*;
//! #
//! # #[spinel_packed("CiiLUE")]
//! # #[derive(Debug)]
//! # pub struct SomePackedData<'a> {
//! #     foo: u8,
//! #     bar: u32,
//! #     blah: u32,
//! #     bleh: u32,
//! #     name: &'a str,
//! #     addr: spinel_pack::EUI64,
//! # }
//! #
//! # fn main() -> std::io::Result<()> {
//! let data = SomePackedData {
//!     foo: 10,
//!     bar: 20,
//!     blah: 30,
//!     bleh: 40,
//!     name: "This is a string",
//!     addr: spinel_pack::EUI64([0,0,0,0,0,0,0,0]),
//! };
//!
//! let packed: Vec<u8> = data.try_packed()?;
//! # let _ = packed;
//! # Ok(())
//! # }
//! ```
//!
//! Packing into an existing array:
//!
//! ```
//! # use spinel_pack::prelude::*;
//! #
//! # #[spinel_packed("CiiLUE")]
//! # #[derive(Debug)]
//! # pub struct SomePackedData<'a> {
//! #     foo: u8,
//! #     bar: u32,
//! #     blah: u32,
//! #     bleh: u32,
//! #     name: &'a str,
//! #     addr: spinel_pack::EUI64,
//! # }
//! #
//! # fn main() -> std::io::Result<()> {
//! let data = SomePackedData {
//!     foo: 10,
//!     bar: 20,
//!     blah: 30,
//!     bleh: 40,
//!     name: "This is a string",
//!     addr: spinel_pack::EUI64([0,0,0,0,0,0,0,0]),
//! };
//!
//! let mut bytes = [0u8; 500];
//! let length = data.try_pack(&mut &mut bytes[..])?;
//! # let _ = length;
//! # Ok(())
//! # }
//! ```
//!
//! Unpacking:
//!
//! ```
//! # use spinel_pack::prelude::*;
//! #
//! # #[spinel_packed("CiiLUE")]
//! # #[derive(Debug)]
//! # pub struct SomePackedData<'a> {
//! #     foo: u8,
//! #     bar: u32,
//! #     blah: u32,
//! #     bleh: u32,
//! #     name: &'a str,
//! #     addr: spinel_pack::EUI64,
//! # }
//! #
//! # fn main() -> anyhow::Result<()> {
//! let bytes: &[u8] = &[0x01, 0x02, 0x03, 0xef, 0xbe, 0xad, 0xde, 0x31, 0x32, 0x33, 0x00, 0x02,
//!                      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff];
//!
//! let data = SomePackedData::try_unpack(&mut bytes.iter())?;
//!
//! assert_eq!(data.foo, 1);
//! assert_eq!(data.bar, 2);
//! assert_eq!(data.blah, 3);
//! assert_eq!(data.bleh, 0xdeadbeef);
//! assert_eq!(data.name, "123");
//! assert_eq!(data.addr, spinel_pack::EUI64([0x02,0xff,0xff,0xff,0xff,0xff,0xff,0xff]));
//! # Ok(())
//! # }
//! ```
//!
//! Spinel packed structs can be nested:
//!
//! ```
//! # use spinel_pack::prelude::*;
//! #
//! # #[spinel_packed("CiiLUE")]
//! # #[derive(Debug, Eq, PartialEq)]
//! # pub struct SomePackedData<'a> {
//! #     foo: u8,
//! #     bar: u32,
//! #     blah: u32,
//! #     bleh: u32,
//! #     name: &'a str,
//! #     addr: spinel_pack::EUI64,
//! # }
//! #
//! #[spinel_packed("idU")]
//! #[derive(Debug, Eq, PartialEq)]
//! pub struct SomeNestedData<'a> {
//!     foo: u32,
//!     test_struct_1: SomePackedData<'a>,
//!     name: String,
//! }
//! ```
//!
//! Each field of a struct must have an associated format type indicator:
//!
//! ```compile_fail
//! # use spinel_pack::prelude::*;
//! #[spinel_packed("i")]
//! #[derive(Debug, Eq, PartialEq)]
//! pub struct SomePackedData {
//!     foo: u32,
//!     data: &'a [u8], // Compiler error, no format type
//! }
//! ```
//!
//! Likewise, each format type indicator must have a field:
//!
//! ```compile_fail
//! # use spinel_pack::prelude::*;
//! #[spinel_packed("id")]
//! #[derive(Debug, Eq, PartialEq)]
//! pub struct SomePackedData {
//!     foo: u32,
//! } // Compiler error, missing field for 'd'
//! ```
//!
//! ```compile_fail
//! #use spinel_pack::prelude::*;
//! #[spinel_packed("id")]
//! #[derive(Debug, Eq, PartialEq)]
//! pub struct SomePackedData; // Compiler error, no fields at all
//! ```
//!
//! Using `spinel_write!()`:
//!
//! ```
//! # use spinel_pack::prelude::*;
//! let mut target: Vec<u8> = vec![];
//!
//! spinel_write!(&mut target, "isc", 1, 2, 3)
//!     .expect("spinel_write failed");
//!
//! assert_eq!(target, vec![1u8,2u8,0u8,3u8]);
//! ```
//!
//! Using `spinel_read!()`:
//!
//! ```
//! # use spinel_pack::prelude::*;
//! // The data to parse.
//! let bytes: &[u8] = &[
//!     0x01, 0x02, 0x83, 0x03, 0xef, 0xbe, 0xad, 0xde,
//!     0x31, 0x32, 0x33, 0x00, 0x02, 0xf1, 0xf2, 0xf3,
//!     0xf4, 0xf5, 0xf6, 0xf7,
//! ];
//!
//! // The variables that we will place
//! // the parsed values into. Note that
//! // currently these need to be initialized
//! // before they can be used with `spinel_read`.
//! let mut foo: u8 = 0;
//! let mut bar: u32 = 0;
//! let mut blah: u32 = 0;
//! let mut bleh: u32 = 0;
//! let mut name: String = Default::default();
//! let mut addr: spinel_pack::EUI64 = Default::default();
//!
//! // Parse the data.
//! spinel_read!(&mut bytes.iter(), "CiiLUE", foo, bar, blah, bleh, name, addr)
//!     .expect("spinel_read failed");
//!
//! // Verify that the variables match
//! // the values we expect.
//! assert_eq!(foo, 1);
//! assert_eq!(bar, 2);
//! assert_eq!(blah, 387);
//! assert_eq!(bleh, 0xdeadbeef);
//! assert_eq!(name, "123");
//! assert_eq!(addr, spinel_pack::EUI64([0x02, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7]));
//! ```

#![warn(missing_docs)]
#![warn(missing_debug_implementations)]
#![warn(rust_2018_idioms)]
#![warn(clippy::all)]
// TODO(fxbug.dev/49842): Remove
#![allow(elided_lifetimes_in_paths)]
#![allow(unused_mut)]

mod eui;
mod primitives;

use proc_macro_hack::proc_macro_hack;
use std::fmt::Debug;
use std::io;

pub use eui::{EUI48, EUI64};

/// Attribute macro which takes a Spinel format string as an argument
/// and automatically defines the `TryPack`/`TryUnpack` traits for the
/// given struct.
///
/// The full list of traits implemented by this macro include:
///
/// * `TryPack`/`TryUnpack`
/// * `TryPackAs<SpinelDataWlen>`/`TryUnpackAs<SpinelDataWlen>`
/// * `TryPackAs<[u8]>`/`TryUnpackAs<[u8]>`
///
/// Additionally, if no lifetimes are specified, the following trait is
/// also implemented:
///
/// * `TryOwnedUnpack`
///
pub use spinel_pack_macros::spinel_packed;

/// In-line proc macro for writing spinel-formatted data fields to a type
/// implementing `std::io::Write`.
///
/// ## Example ##
///
/// ```
/// # use spinel_pack::prelude::*;
/// let mut target: Vec<u8> = vec![];
///
/// spinel_write!(&mut target, "isc", 1, 2, 3)
///     .expect("spinel_write failed");
///
/// assert_eq!(target, vec![1u8,2u8,0u8,3u8]);
/// ```
#[proc_macro_hack]
pub use spinel_pack_macros::spinel_write;

/// In-line proc macro for determining the written length of spinel-formatted
/// data fields.
///
/// ## Example ##
///
/// ```
/// # use spinel_pack::prelude::*;
/// let len = spinel_write_len!("isc", 1, 2, 3)
///     .expect("spinel_write_len failed");
///
/// assert_eq!(len, 4);
/// ```
#[proc_macro_hack]
pub use spinel_pack_macros::spinel_write_len;

/// In-line proc macro for parsing spinel-formatted data fields from
/// a byte slice iterator.
///
/// ## Example ##
///
/// ```
/// # use spinel_pack::prelude::*;
/// // The data to parse.
/// let bytes: &[u8] = &[
///     0x01, 0x02, 0x83, 0x03, 0xef, 0xbe, 0xad, 0xde,
///     0x31, 0x32, 0x33, 0x00, 0x02, 0xf1, 0xf2, 0xf3,
///     0xf4, 0xf5, 0xf6, 0xf7,
/// ];
///
/// // The variables that we will place
/// // the parsed values into. Note that
/// // currently these need to be initialized
/// // before they can be used with `spinel_read`.
/// let mut foo: u8 = 0;
/// let mut bar: u32 = 0;
/// let mut blah: u32 = 0;
/// let mut bleh: u32 = 0;
/// let mut name: String = Default::default();
/// let mut addr: spinel_pack::EUI64 = Default::default();
///
/// // Parse the data.
/// spinel_read!(&mut bytes.iter(), "CiiLUE", foo, bar, blah, bleh, name, addr)
///     .expect("spinel_read failed");
///
/// // Verify that the variables match
/// // the values we expect.
/// assert_eq!(foo, 1);
/// assert_eq!(bar, 2);
/// assert_eq!(blah, 387);
/// assert_eq!(bleh, 0xdeadbeef);
/// assert_eq!(name, "123");
/// assert_eq!(addr, spinel_pack::EUI64([0x02, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7]));
/// ```
#[proc_macro_hack]
pub use spinel_pack_macros::spinel_read;

/// Error type for unpacking operations.
///
/// These errors end up being wrapped in an `anyhow::Error` type.
#[derive(Debug, Eq, PartialEq, Hash, thiserror::Error)]
pub enum UnpackingError {
    /// A length field in the data overflowed the length of its container.
    #[error("InvalidInternalLength")]
    InvalidInternalLength,

    /// One or more of the encoded fields contained an invalid value.
    #[error("InvalidValue")]
    InvalidValue,

    /// The text string was not zero terminated.
    #[error("UnterminatedString")]
    UnterminatedString,
}

/// Marker trait for types which always serialize to the same length.
pub trait SpinelFixedLen {
    /// The length of the type, in bytes.
    const FIXED_LEN: usize;
}

/// Trait implemented by data types that support being serialized
/// to a spinel-based byte encoding.
pub trait TryPack {
    /// Uses Spinel encoding to serialize to a given `std::io::Write` reference.
    fn try_pack<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize>;

    /// Calculates how many bytes this type will use when serialized.
    fn pack_len(&self) -> io::Result<usize>;

    /// Uses Spinel array encoding to serialize to a given `std::io::Write` reference.
    ///
    /// Array encoding is occasionally different than single-value encoding,
    /// hence the need for a separate method.
    ///
    /// Default behavior is the same as `try_pack()`.
    fn try_array_pack<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize> {
        self.try_pack(buffer)
    }

    /// Calculates how many bytes this type will use when serialized into an array.
    ///
    /// Array encoding is occasionally different than single-value encoding,
    /// hence the need for a separate method.
    ///
    /// Default behavior is the same as `pack_len()`.
    fn array_pack_len(&self) -> io::Result<usize> {
        self.pack_len()
    }

    /// Convenience method which serializes to a new `Vec<u8>`.
    fn try_packed(&self) -> io::Result<Vec<u8>> {
        let mut packed = Vec::with_capacity(self.pack_len()?);
        self.try_pack(&mut packed)?;
        Ok(packed)
    }
}

/// Trait implemented by data types that support being serialized
/// to a specific spinel-based byte encoding, based on the marker type.
///
/// The generic parameter `Marker` is effectiely a part of the name of the
/// trait and is not used directly by the trait. Types may implement more than
/// one instance of this trait, each with a different marker type.
/// For example, structs that use the `spinel_packed` attribute macro
/// will implement both `TryPackAs<[u8]>` and `TryPackAs<SpinelDataWlen>`
/// for handling the `D` and `d` Spinel field formats respectively when
/// serializing.
pub trait TryPackAs<Marker: ?Sized> {
    /// Uses Spinel encoding to serialize to a given `std::io::Write` reference as the
    /// Spinel type identified by `Marker`.
    fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize>;

    /// Calculates how many bytes this type will use when serialized.
    fn pack_as_len(&self) -> io::Result<usize>;
}

/// Trait for unpacking a spinel-encoded buffer to a specific type.
///
/// Similar to `TryOwnedUnpack`, except that it can also unpack into borrowed
/// types, like `&[u8]` and `&str`.
pub trait TryUnpack<'a> {
    /// The type of the unpacked result. This can be the same as `Self`,
    /// but in some cases it can be different. This is because `Self` is
    /// a *marker type*, and may not even be `Sized`. For example, if `Self` is
    /// `SpinelUint`, then `Unpacked` would be `u32` (because `SpinelUint` is just
    /// a marker trait indicating a variably-sized unsigned integer).
    type Unpacked: Send + Sized + Debug;

    /// Attempts to decode the data at the given iterator into an instance
    /// of `Self`.
    fn try_unpack(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self::Unpacked>;

    /// Attempts to decode an item from an array at the given iterator into
    /// an instance of `Self`.
    ///
    /// Array encoding is occasionally different than single-value encoding,
    /// hence the need for a separate method.
    ///
    /// Default behavior is the same as `try_unpack()`.
    fn try_array_unpack(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self::Unpacked> {
        Self::try_unpack(iter)
    }

    /// Convenience method for unpacking directly from a borrowed slice.
    fn try_unpack_from_slice(slice: &'a [u8]) -> anyhow::Result<Self::Unpacked> {
        Self::try_unpack(&mut slice.iter())
    }
}

/// Trait for unpacking only into owned types, like `Vec<u8>` or `String`.
///
/// Similar to `TryUnpack`, except that this trait cannot unpack into borrowed
/// types like `&[u8]` or `&str`.
///
/// If you have a `TryOwnedUnpack` implementation, you can automatically implement
/// `TryUnpack` using the `impl_try_unpack_for_owned!` macro.
pub trait TryOwnedUnpack: Send {
    /// The type of the unpacked result. This can be the same as `Self`,
    /// but in some cases it can be different. This is because `Self` is
    /// a *marker type*, and may not even be `Sized`. For example, if `Self` is
    /// `SpinelUint`, then `Unpacked` would be `u32` (because `SpinelUint` is just
    /// a marker trait indicating a variably-sized unsigned integer).
    type Unpacked: Send + Sized + Debug;

    /// Attempts to decode the data at the given iterator into an instance
    /// of `Self`, where `Self` must be an "owned" type.
    fn try_owned_unpack(iter: &mut std::slice::Iter<'_, u8>) -> anyhow::Result<Self::Unpacked>;

    /// Attempts to decode an item from an array at the given iterator into
    /// an instance of `Self`, where `Self` must be an "owned" type.
    ///
    /// Array encoding is occasionally different than single-value encoding,
    /// hence the need for a separate method.
    ///
    /// Default behavior is the same as `try_owned_unpack()`.
    fn try_array_owned_unpack(
        iter: &mut std::slice::Iter<'_, u8>,
    ) -> anyhow::Result<Self::Unpacked> {
        Self::try_owned_unpack(iter)
    }

    /// Convenience method for unpacking directly from a borrowed slice.
    fn try_owned_unpack_from_slice(slice: &'_ [u8]) -> anyhow::Result<Self::Unpacked> {
        Self::try_owned_unpack(&mut slice.iter())
    }
}

/// Trait for unpacking a spinel-encoded buffer to a specific type when the field
/// type is known.
///
/// The generic parameter `Marker` is effectiely a part of the name of the
/// trait and is not used directly by the trait. Types may implement more than
/// one instance of this trait, each with a different marker type.
/// For example, structs that use the `spinel_packed` attribute macro
/// will implement both `TryUnpackAs<[u8]>` and `TryUnpackAs<SpinelDataWlen>`
/// for handling the `D` and `d` Spinel field formats respectively when
/// deserializing.
pub trait TryUnpackAs<'a, Marker: ?Sized>: Sized {
    /// Attempts to decode the data (with a format determined by `Marker`)  at the given
    /// iterator into an instance of `Self`.
    fn try_unpack_as(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self>;

    /// Convenience method for unpacking directly from a borrowed slice.
    fn try_unpack_as_from_slice(slice: &'a [u8]) -> anyhow::Result<Self> {
        Self::try_unpack_as(&mut slice.iter())
    }
}

/// Provides an automatic implementation of [`TryUnpack`] when
/// wrapped around an implementation of [`TryOwnedUnpack`].
///
/// ## Example ##
///
/// The following usage will create an implementation of the `TryUnpack`
/// trait for `Vec<u8>` that uses the given implementation of `TryOwnedUnpack`:
///
/// ```no_compile
/// # use spinel_pack::{TryOwnedUnpack, prelude::*};
/// impl_try_unpack_for_owned! {
///     impl TryOwnedUnpack for Vec<u8> {
///         type Unpacked = Self;
///         fn try_owned_unpack(iter: &mut std::slice::Iter<'_, u8>) -> anyhow::Result<Self::Unpacked> {
///             Ok(<&[u8]>::try_unpack(iter)?.to_owned())
///         }
///     }
/// }
/// ```
#[macro_export]
macro_rules! impl_try_unpack_for_owned(
    (impl TryOwnedUnpack for $t:ty { $($rest:tt)* }) => {
        impl_try_unpack_for_owned!($t);
        impl TryOwnedUnpack for $t { $($rest)* }
    };
    ($t:ty) => {
        impl<'a> TryUnpack<'a> for $t {
            type Unpacked = <$t as TryOwnedUnpack>::Unpacked;
            fn try_unpack(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self::Unpacked> {
                <$t as TryOwnedUnpack>::try_owned_unpack(iter)
            }
        }
    };
);

/// Marker type used to specify integers encoded with Spinel's variable-length unsigned
/// integer encoding.
///
/// This type is necessary because `u32` is defined to mean a fixed-length
/// encoding, whereas this type can be encoded with a variable length.
///
/// This type has no size and is used only with the [`TryPackAs`]/[`TryUnpackAs`] traits
/// and in the base type for [`TryUnpack`]/[`TryOwnedUnpack`].
#[derive(Debug)]
pub enum SpinelUint {}

/// Marker type used to specify data fields that are prepended with its length.
///
/// This type is necessary because `[u8]` is already defined to assume the
/// remaining length of the buffer.
///
/// This type has no size and is used only with the [`TryPackAs`]/[`TryUnpackAs`] traits
/// and in the base type for [`TryUnpack`]/[`TryOwnedUnpack`].
#[derive(Debug)]
pub enum SpinelDataWlen {}

/// Prelude module intended for blanket inclusion to make the crate
/// easier to use.
///
/// ```
/// # #[allow(unused)]
/// use spinel_pack::prelude::*;
/// ```
pub mod prelude {
    pub use super::TryOwnedUnpack as _;
    pub use super::TryPack as _;
    pub use super::TryPackAs as _;
    pub use super::TryUnpack as _;
    pub use super::TryUnpackAs as _;
    pub use impl_try_unpack_for_owned;
    pub use spinel_pack_macros::spinel_packed;
    pub use spinel_read;
    pub use spinel_write;
    pub use spinel_write_len;
}

#[cfg(test)]
use crate as spinel_pack;

#[cfg(test)]
mod tests {

    use super::*;

    #[spinel_packed("CiiLUE")]
    #[derive(Debug, Eq, PartialEq)]
    pub struct TestStruct1<'a> {
        foo: u8,
        bar: u32,
        blah: u32,
        bleh: u32,
        name: &'a str,
        addr: spinel_pack::EUI64,
    }

    #[test]
    fn test_spinel_write_1() {
        let bytes: &[u8] = &[
            0x01, 0x02, 0x83, 0x03, 0xef, 0xbe, 0xad, 0xde, 0x31, 0x32, 0x33, 0x00, 0x02, 0xf1,
            0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
        ];

        let mut target: Vec<u8> = vec![];

        let _x = spinel_write!(
            &mut target,
            "CiiLUE",
            1,
            2,
            387,
            0xdeadbeef,
            "123",
            spinel_pack::EUI64([0x02, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7])
        )
        .unwrap();

        assert_eq!(bytes, target.as_slice());
    }

    #[test]
    fn test_spinel_read_1() {
        let bytes: &[u8] = &[
            0x01, 0x02, 0x83, 0x03, 0xef, 0xbe, 0xad, 0xde, 0x31, 0x32, 0x33, 0x00, 0x02, 0xf1,
            0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
        ];

        let mut foo: u8 = 0;
        let mut bar: u32 = 0;
        let mut blah: u32 = 0;
        let mut bleh: u32 = 0;
        let mut name: String = Default::default();
        let mut addr: EUI64 = Default::default();
        spinel_read!(&mut bytes.iter(), "CiiLUE", foo, bar, blah, bleh, name, addr).unwrap();

        assert_eq!(foo, 1);
        assert_eq!(bar, 2);
        assert_eq!(blah, 387);
        assert_eq!(bleh, 0xdeadbeef);
        assert_eq!(name, "123");
        assert_eq!(addr, spinel_pack::EUI64([0x02, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7]));
    }

    #[test]
    fn test_struct_1() {
        let bytes: &[u8] = &[
            0x01, 0x02, 0x83, 0x03, 0xef, 0xbe, 0xad, 0xde, 0x31, 0x32, 0x33, 0x00, 0x02, 0xf1,
            0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
        ];

        let data = TestStruct1::try_unpack(&mut bytes.iter()).expect("unpack failed");

        assert_eq!(data.foo, 1);
        assert_eq!(data.bar, 2);
        assert_eq!(data.blah, 387);
        assert_eq!(data.bleh, 0xdeadbeef);
        assert_eq!(data.name, "123");
        assert_eq!(data.addr, spinel_pack::EUI64([0x02, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7]));
    }

    #[spinel_packed("idU")]
    #[derive(Debug, Eq, PartialEq)]
    pub struct TestStruct2<'a> {
        foo: u32,
        test_struct_1: TestStruct1<'a>,
        name: String,
    }

    #[test]
    fn test_struct_2() {
        let struct_2 = TestStruct2 {
            foo: 31337,
            test_struct_1: TestStruct1 {
                foo: 100,
                bar: 200,
                blah: 300,
                bleh: 400,
                name: "Test Struct 1",
                addr: spinel_pack::EUI64([0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff]),
            },
            name: "Test Struct 2".to_string(),
        };

        let packed = struct_2.try_packed().unwrap();

        println!("packed: {:?}", packed);

        let unpacked = TestStruct2::try_unpack(&mut packed.iter()).unwrap();

        assert_eq!(struct_2, unpacked);
    }

    #[spinel_packed("idU")]
    #[derive(Debug, Eq, PartialEq)]
    pub struct TestStruct3<'a>(pub u32, pub TestStruct1<'a>, pub String);

    #[test]
    fn test_struct_3() {
        let struct_3 = TestStruct3(
            31337,
            TestStruct1 {
                foo: 100,
                bar: 200,
                blah: 300,
                bleh: 400,
                name: "Test Struct 1",
                addr: spinel_pack::EUI64([0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff]),
            },
            "Test Struct 2".to_string(),
        );

        let packed = struct_3.try_packed().unwrap();

        println!("packed: {:?}", packed);

        let unpacked = TestStruct3::try_unpack(&mut packed.iter()).unwrap();

        assert_eq!(struct_3, unpacked);
    }

    #[spinel_packed("bCcSsLlXxidD")]
    #[derive(Debug, Eq, PartialEq)]
    pub struct TestStruct4(bool, u8, i8, u16, i16, u32, i32, u64, i64, u32, Vec<u8>, Vec<u8>);

    #[test]
    fn test_struct_4() {
        let struct_4 = TestStruct4(
            false,
            123,
            -123,
            1337,
            -1337,
            41337,
            -41337,
            123123123123,
            -123123123123,
            31337,
            vec![0xde, 0xad, 0xbe, 0xef],
            vec![0xba, 0xdc, 0x0f, 0xfe],
        );

        let packed = struct_4.try_packed().unwrap();

        println!("packed: {:?}", packed);

        let unpacked = TestStruct4::try_unpack(&mut packed.iter()).unwrap();

        assert_eq!(struct_4, unpacked);
    }
}
