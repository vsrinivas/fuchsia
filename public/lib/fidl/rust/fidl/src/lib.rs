// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library and runtime for fidl bindings.

#![feature(futures_api, pin, arbitrary_self_types)]
#![deny(missing_docs)]
#![deny(warnings)]

extern crate fuchsia_zircon as zircon;
extern crate byteorder;
#[macro_use] extern crate failure;
extern crate fuchsia_async as async;
#[macro_use] extern crate futures;
extern crate parking_lot;
extern crate slab;

#[macro_use]
pub mod encoding2;
mod error;
pub mod client2;
pub mod endpoints2;

pub use error::{Error, Result};

use futures::task::{self, AtomicWaker};
use std::sync::atomic::{self, AtomicBool};

/// A type used from the innards of server implementations
pub struct ServeInner {
    waker: AtomicWaker,
    shutdown: AtomicBool,
    channel: async::Channel,
}

impl ServeInner {
    /// Create a new set of server innards.
    pub fn new(channel: async::Channel) -> Self {
        let waker = AtomicWaker::new();
        let shutdown = AtomicBool::new(false);
        ServeInner { waker, shutdown, channel }
    }

    /// Get a reference to the inner channel.
    pub fn channel(&self) -> &async::Channel {
        &self.channel
    }

    /// Set the server to shutdown.
    pub fn shutdown(&self) {
        self.shutdown.store(true, atomic::Ordering::Relaxed);
        self.waker.wake();
    }

    /// Check if the server has been set to shutdown.
    pub fn poll_shutdown(&self, cx: &mut task::Context) -> bool {
        if self.shutdown.load(atomic::Ordering::Relaxed) {
            return true;
        }
        self.waker.register(cx.waker());
        self.shutdown.load(atomic::Ordering::Relaxed)
    }
}

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
