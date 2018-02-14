// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library and runtime for fidl bindings.

#![deny(missing_docs)]
#![deny(warnings)]

extern crate fuchsia_zircon as zircon;
extern crate byteorder;
#[macro_use] extern crate failure;
extern crate futures;
extern crate slab;
extern crate tokio_core;
extern crate tokio_fuchsia;

#[macro_use]
mod encoding;
pub mod encoding2;
mod message;
mod error;
mod server;
mod interface;
mod client;
mod endpoints;

pub use encoding::{Encodable, Decodable, EncodablePtr, DecodablePtr, EncodableType};
pub use encoding::{CodableUnion, EncodableNullable, DecodableNullable};
pub use encoding::{encode_handle, decode_handle};
pub use message::{EncodeBuf, DecodeBuf, MsgType};
pub use error::{Error, Result, ErrorOrClose};
pub use server::{CloseChannel, Stub, Server};
pub use interface::InterfacePtr;
pub use client::{Client, FidlService};
pub use endpoints::{ClientEnd, ServerEnd};

/// A specialized `Box<Future<...>>` type for FIDL.
/// This is a convenience to avoid writing
/// `Future<Item = I, Error = CloseChannel> + Send`.
/// The error type indicates various FIDL protocol errors, as well as general-purpose IO
/// errors such as a closed channel.
pub type BoxFuture<Item> = Box<futures::Future<Item = Item, Error = Error> + Send>;

/// A specialized `Future<...>` type for FIDL server implementations.
/// This is a convenience to avoid writing
/// `Future<Item = I, Error = CloseChannel>>`.
/// Errors in this `Future` should require no extra handling, and upon error the type should
/// be dropped.
pub trait ServerFuture<Item>: futures::Future<Item = Item, Error = CloseChannel> {}
impl<T, Item> ServerFuture<Item> for T
    where T: futures::Future<Item = Item, Error = CloseChannel> {}

/// A specialized `Box<Future<...>>` type for FIDL server implementations.
/// This is a convenience to avoid writing
/// `Box<Future<Item = I, Error = CloseChannel>>`.
/// Errors in this `Future` should require no extra handling, and upon error the type should
/// be dropped.
pub type BoxServerFuture<Item> = Box<futures::Future<Item = Item, Error = CloseChannel>>;

/// A specialized `Future<...>` type for FIDL server implementations.
/// This is a convenience to avoid writing
/// `future::FutureResult<Item = I, Error = CloseChannel>>`.
pub type ServerImmediate<Item> = futures::future::FutureResult<Item, CloseChannel>;

/// A specialized `Future<...>` type for FIDL.
/// This is a convenience to avoid writing
/// `Future<Item = I, Error = fidl::Error> + Send`.
/// The error type indicates various FIDL protocol errors, as well as general-purpose IO
/// errors such as a closed channel.
pub trait Future<Item>: futures::Future<Item = Item, Error = Error> + Send {}
impl<T, Item> Future<Item> for T
    where T: futures::Future<Item = Item, Error = Error> + Send {}

#[macro_export]
macro_rules! fidl_enum {
    ($typename:ident, [$($name:ident = $value:expr;)*]) => {
        #[allow(non_upper_case_globals)]
        impl $typename {
            $(
                pub const $name: $typename = $typename($value);
            )*

            #[allow(unreachable_patterns)]
            fn fidl_enum_name(&self) -> Option<&'static str> {
                match self.0 {
                    $(
                        $value => Some(stringify!($name)),
                    )*
                    _ => None,
                }
            }
        }

        impl ::std::fmt::Debug for $typename {
            fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
                f.write_str(concat!(stringify!($typename), "("))?;
                match self.fidl_enum_name() {
                    Some(name) => f.write_str(&name)?,
                    None => ::std::fmt::Debug::fmt(&self.0, f)?,
                }
                f.write_str(")")
            }
        }
    }
}
