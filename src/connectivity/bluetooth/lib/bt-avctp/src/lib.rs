// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "128"]

use {fuchsia_zircon as zx, std::result, thiserror::Error};

mod avc;
mod avctp;

pub use crate::avctp::{
    Command as AvctpCommand, CommandStream as AvctpCommandStream, MessageType as AvctpMessageType,
    PacketType as AvctpPacketType, Peer as AvctpPeer,
};

pub use crate::avc::{
    Command as AvcCommand, CommandResponse as AvcCommandResponse,
    CommandStream as AvcCommandStream, CommandType as AvcCommandType, OpCode as AvcOpCode,
    PacketType as AvcPacketType, Peer as AvcPeer, ResponseType as AvcResponseType,
};

/// The error type of the AVCTP library.
#[derive(Error, Debug, PartialEq)]
pub enum Error {
    /// The value that was sent on the wire was out of range.
    #[error("Value was out of range")]
    OutOfRange,

    /// The profile identifier sent was returned as invalid by the peer.
    #[error("Invalid profile id")]
    InvalidProfileId,

    /// The header was invalid when parsing a message from the peer.
    #[error("Invalid Header for a AVCTP message")]
    InvalidHeader,

    /// The body format was invalid when parsing a message from the peer.
    #[error("Failed to parse AVCTP message contents")]
    InvalidMessage,

    /// The remote end failed to respond to this command in time.
    #[error("Command timed out")]
    Timeout,

    /// The distant peer has disconnected.
    #[error("Peer has disconnected")]
    PeerDisconnected,

    /// Sent if a Command Future is polled after it's already completed
    #[error("Command Response has already been received")]
    AlreadyReceived,

    /// Encountered an IO error reading from the peer.
    #[error("Encountered an IO error reading from the peer: {}", _0)]
    PeerRead(zx::Status),

    /// Encountered an IO error reading from the peer.
    #[error("Encountered an IO error writing to the peer: {}", _0)]
    PeerWrite(zx::Status),

    /// A message couldn't be encoded.
    #[error("Encountered an error encoding a message")]
    Encoding,

    /// An error has been detected, and the request that is being handled
    /// should be rejected with the error code given.
    #[error("Invalid request detected")]
    RequestInvalid,

    /// The response command type is not valid.
    #[error("Command type is not a response")]
    ResponseTypeInvalid,

    /// The response command was unexpected
    #[error("Response command type is unexpected")]
    UnexpectedResponse,

    #[doc(hidden)]
    #[error("__Nonexhaustive error should never be created.")]
    __Nonexhaustive,
}

/// Result type for AVCTP, using avctp::Error
pub(crate) type Result<T> = result::Result<T, Error>;
