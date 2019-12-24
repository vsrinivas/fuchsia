// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Qmux Errors, the transport layer for QMI-based modems
use {fuchsia_zircon as zx, thiserror::Error};

#[derive(Debug, Error)]
pub enum QmuxError {
    /// An endpoint encountered an IO error reading a response from a channel.
    #[error("A FIDL client encountered an IO error reading a response from a channel: {}", _0)]
    ClientRead(zx::Status),

    /// A endpoint encountered an IO error writing a request to a channel.
    #[error("A FIDL client encountered an IO error writing a request into a channel: {}", _0)]
    ClientWrite(zx::Status),

    /// Invalid buffer.
    #[error("Invalid QMI buffer contents")]
    Invalid,

    /// A future was polled after it had already completed.
    #[error("A QMI future was polled after it had already completed.")]
    PollAfterCompletion,

    /// A Service or Client has not been initialized, but a transaction for it exists
    #[error("A Transaction for an non-existent Service/Client pair")]
    InvalidSvcOrClient,

    /// No Client.
    #[error("Failed to negotiate creating a client with the modem")]
    NoClient,

    /// No Transport.
    #[error("No channel to communicate with transport layer")]
    NoTransport,
}
