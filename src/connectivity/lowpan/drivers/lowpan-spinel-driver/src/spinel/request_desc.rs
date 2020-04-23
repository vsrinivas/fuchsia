// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use anyhow::Error;
use core::fmt::Debug;
use std::io;

/// Trait that builds a request to send to a device
/// and handles the response.
pub trait RequestDesc: Send + Debug {
    /// The type returned by a successful call to
    /// [`FrameHandler::send_request`].
    ///
    /// This may be `()`.
    type Result: Send + Sized + Debug;

    /// Writes out all of the bytes for this request to `buffer`, with
    /// the exception of the header byte.
    fn write_request(&self, buffer: &mut dyn io::Write) -> io::Result<()>;

    /// Called after the request has been confirmed to be sent over the wire.
    fn on_request_sent(&self) {}

    /// Called when a response has been received for our request.
    ///
    /// If the transaction was cancelled, this is called with an
    /// argument of `Err(Cancelled)`.
    fn on_response(
        self,
        response: Result<SpinelFrameRef<'_>, Canceled>,
    ) -> Result<Self::Result, Error>;
}

pub trait RequestDescExt: RequestDesc + Sized {}

// Blanket implementation
impl<T: RequestDesc + Sized> RequestDescExt for T {}
