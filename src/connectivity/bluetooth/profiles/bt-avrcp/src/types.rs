// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_avctp::Error as AvctpError,
    failure::{Error, Fail},
};

use crate::packets::Error as PacketError;

// TODO(BT-2197): change to the BT shared peer id type when the BrEdr protocol changes over.
pub type PeerId = String;

/// The error types for peer management.
#[derive(Fail, Debug)]
pub enum PeerError {
    /// Error encoding/decoding packet
    #[fail(display = "Packet encoding/decoding error: {:?}", _0)]
    PacketError(PacketError),

    /// Error in protocol layer
    #[fail(display = "Protocol layer error: {:?}", _0)]
    #[allow(dead_code)]
    AvctpError(AvctpError),

    #[fail(display = "Remote device was not connected")]
    RemoteNotFound,

    #[fail(display = "Remote command is unsupported")]
    CommandNotSupported,

    #[fail(display = "Remote command rejected")]
    CommandFailed,

    #[fail(display = "Unable to connect")]
    ConnectionFailure(#[cause] Error),

    #[fail(display = "Unexpected response to command")]
    UnexpectedResponse,

    #[fail(display = "Generic errors")]
    GenericError(#[cause] Error),

    #[doc(hidden)]
    #[fail(display = "__Nonexhaustive error should never be created.")]
    __Nonexhaustive,
}

impl From<AvctpError> for PeerError {
    fn from(error: AvctpError) -> Self {
        PeerError::AvctpError(error)
    }
}

impl From<PacketError> for PeerError {
    fn from(error: PacketError) -> Self {
        PeerError::PacketError(error)
    }
}

impl From<Error> for PeerError {
    fn from(error: Error) -> Self {
        PeerError::GenericError(error)
    }
}
