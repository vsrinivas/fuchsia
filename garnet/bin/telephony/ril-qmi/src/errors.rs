// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Qmux Errors, the transport layer for QMI-based modems
use {failure::Fail, fuchsia_zircon as zx};

#[derive(Debug, Fail)]
pub enum QmuxError {
    /// An endpoint encountered an IO error reading a response from a channel.
    #[fail(
        display = "A FIDL client encountered an IO error reading a response from a channel: {}",
        _0
    )]
    ClientRead(#[cause] zx::Status),

    /// A endpoint encountered an IO error writing a request to a channel.
    #[fail(
        display = "A FIDL client encountered an IO error writing a request into a channel: {}",
        _0
    )]
    ClientWrite(#[cause] zx::Status),

    /// Invalid buffer.
    #[fail(display = "Invalid QMI buffer contents")]
    Invalid,

    /// A future was polled after it had already completed.
    #[fail(display = "A QMI future was polled after it had already completed.")]
    PollAfterCompletion,

    /// A Service or Client has not been initilialized, but a transaction for it exists
    #[fail(display = "A Transaction for an non-existant Service/Client pair")]
    InvalidSvcOrClient,

    /// No Client.
    #[fail(display = "Failed to negotatiate creating a client with the modem")]
    NoClient,

    /// No Transport.
    #[fail(display = "No channel to communicate with transport layer")]
    NoTransport,
}
