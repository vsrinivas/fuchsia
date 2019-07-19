// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library and runtime for fidl bindings.

#![feature(async_await, await_macro)]
#![deny(missing_docs)]
#![deny(warnings)]

#[macro_use]
pub mod encoding;
pub mod client;
pub mod endpoints;
pub mod handle;
pub mod server;

mod error;
pub use self::error::{Error, Result};
// Used to generate the types for fidl Bits.
#[doc(hidden)]
pub use bitflags::bitflags;

#[cfg(target_os = "fuchsia")]
pub use server::ServeInner;

/*
use {
    fuchsia_async as fasync,
    futures::task::{AtomicWaker, Context},
    std::sync::atomic::{self, AtomicBool},
};
*/
