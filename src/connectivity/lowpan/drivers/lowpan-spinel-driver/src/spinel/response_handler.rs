// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use anyhow::Error;

/// Trait used with [`RequestTracker`] for handling responses.
pub trait ResponseHandler: Send {
    /// Handles a response to a Spinel command.
    fn on_response(&mut self, response: Result<SpinelFrameRef<'_>, Canceled>) -> Result<(), Error>;
}

/// Blanket implementation of `ResponseHandler` for all
/// closures that match the signature of `on_response(...)`
/// and are inside an `Option`.
impl<T> ResponseHandler for Option<T>
where
    T: FnOnce(Result<SpinelFrameRef<'_>, Canceled>) -> Result<(), Error> + Send,
{
    fn on_response(&mut self, response: Result<SpinelFrameRef<'_>, Canceled>) -> Result<(), Error> {
        self.take().map(|x| x(response)).unwrap_or(Err(Error::from(Canceled)))
    }
}

/// Error type indicating that the operation
/// or command was canceled.
#[derive(Debug, Copy, Clone, Eq, PartialEq, thiserror::Error)]
pub struct Canceled;

impl std::fmt::Display for Canceled {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Cancelled")
    }
}
