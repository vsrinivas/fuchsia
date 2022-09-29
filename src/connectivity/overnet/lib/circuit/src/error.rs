// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

pub type Result<T, E = Error> = std::result::Result<T, E>;

#[derive(Error, Debug)]
pub enum Error {
    #[error("Received invalid stream ID")]
    BadStreamId,
    #[error("String `{0}` is too long to be encoded on the wire")]
    StringTooBig(String),
    #[error("Not enough data in buffer, need {0}")]
    BufferTooShort(usize),
    #[error("Got BufferTooShort({0}) from user callback after giving buffer of size {1}")]
    CallbackRejectedBuffer(usize, usize),
    #[error("Bad characters in UTF8 String `{0}`")]
    BadUTF8(String),
    #[error("Connection closed")]
    ConnectionClosed,
    #[error("Version mismatch")]
    VersionMismatch,
    #[error("Protocol mismatch")]
    ProtocolMismatch,
    #[error("Link speed value 255 is reserved")]
    InvalidSpeed,
    #[error("An internal channel closed unexpectedly")]
    InternalPipeBroken,
    #[error("Could not find peer with node ID `{0}`")]
    NoSuchPeer(String),
    #[error("IO Error")]
    IO(#[from] std::io::Error),
}

pub(crate) trait ExtendBufferTooShortError {
    fn extend_buffer_too_short(self, by: usize) -> Self;
}

impl<T> ExtendBufferTooShortError for Result<T> {
    fn extend_buffer_too_short(self, by: usize) -> Self {
        match self {
            Err(Error::BufferTooShort(s)) => Err(Error::BufferTooShort(s + by)),
            other => other,
        }
    }
}
