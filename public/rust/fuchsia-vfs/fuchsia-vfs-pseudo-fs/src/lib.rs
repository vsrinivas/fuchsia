// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A helper to create files backed by in process callbacks.  For example to expose component
//! configuration, debug information or statistics.

#![feature(async_await, await_macro, futures_api)]
#![warn(missing_docs)]

use {
    failure::Error,
    fidl_fuchsia_io::FileRequestStream,
    fuchsia_zircon::Status,
    futures::future::{FusedFuture, Future},
    std::marker::Unpin,
};

#[cfg(test)]
#[macro_use]
mod test_utils;

pub mod file;

/// A base trait for all the pseudo file implementations.  Most clients will probably just use the
/// Future trait to deal with the pseudo files uniformly, but add_request_stream() is necessary to
/// attach new streams to a pseudo file.
pub trait PseudoFile: Future<Output = Result<(), Error>> + Unpin + FusedFuture {
    /// Main entry point to create new connections to this pseudo file.  Specific classes will
    /// provide different implementations.  In case of an error, the stream will be dropped and the
    /// underlying channel will be closed.
    fn add_request_stream(
        &mut self, flags: u32, request_stream: FileRequestStream,
    ) -> Result<(), Status>;
}
