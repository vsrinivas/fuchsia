// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library and runtime for fidl bindings.

#![deny(missing_docs)]
#![deny(warnings)]

extern crate fuchsia_zircon as zircon;
extern crate byteorder;
#[macro_use] extern crate failure;
extern crate fuchsia_async as async;
extern crate futures;
extern crate slab;

#[macro_use]
pub mod encoding2;
mod error;
pub mod client2;
pub mod endpoints2;

pub use error::{Error, Result};

/// A specialized `Box<Future<...>>` type for FIDL.
/// This is a convenience to avoid writing
/// `Future<Item = I, Error = Error> + Send`.
/// The error type indicates various FIDL protocol errors, as well as general-purpose IO
/// errors such as a closed channel.
pub type BoxFuture<Item> = Box<futures::Future<Item = Item, Error = Error> + Send>;

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
