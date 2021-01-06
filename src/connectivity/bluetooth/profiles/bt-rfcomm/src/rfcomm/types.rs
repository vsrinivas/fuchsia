// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_rfcomm::{RfcommError, DLCI},
    futures::future::BoxFuture,
    thiserror::Error,
};

/// Errors that occur during the usage of the RFCOMM component.
#[derive(Error, Debug)]
pub enum Error {
    #[error("DLCI {:?} is not established", .0)]
    ChannelNotEstablished(DLCI),
    #[error("DLCI {:?} is already established", .0)]
    ChannelAlreadyEstablished(DLCI),
    #[error("Multiplexer has already started")]
    MultiplexerAlreadyStarted,
    #[error("Multiplexer has not started")]
    MultiplexerNotStarted,
    #[error("Not implemented")]
    NotImplemented,
    #[error("Rfcomm library error: {:?}", .0)]
    Rfcomm(RfcommError),
    #[error(transparent)]
    Other(#[from] anyhow::Error),
}

impl From<RfcommError> for Error {
    fn from(src: RfcommError) -> Self {
        Self::Rfcomm(src)
    }
}

/// SignaledTasks represent a task or object that is currently running.
/// Typically a SignaledTask will run a background task that is active until dropped.
pub trait SignaledTask {
    /// Returns a Future that finishes when the running task finishes for any reason.
    /// If this task has already finished, the returned future should immediately resolve.
    fn finished(&self) -> BoxFuture<'static, ()>;
}
