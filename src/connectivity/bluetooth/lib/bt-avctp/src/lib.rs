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

/// A decodable type can be created from a byte buffer.
/// The type returned is separate (copied) from the buffer once decoded.
pub trait Decodable<E = Error>: Sized {
    /// Decodes into a new object, or returns an error.
    fn decode(buf: &[u8]) -> result::Result<Self, E>;
}

/// A encodable type can write itself into a byte buffer.
pub trait Encodable<E = Error>: Sized {
    /// Returns the number of bytes necessary to encode |self|
    fn encoded_len(&self) -> usize;

    /// Writes the encoded version of |self| at the start of |buf|
    /// |buf| must be at least size() length.
    fn encode(&self, buf: &mut [u8]) -> result::Result<(), E>;
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_bluetooth::pub_decodable_enum;
    use std::convert::TryFrom;

    pub_decodable_enum! {
        TestEnum<u16, Error, OutOfRange> {
            One => 1,
            Two => 2,
            Max => 65535,
        }
    }

    #[test]
    fn try_from_success() {
        let one = TestEnum::try_from(1);
        assert!(one.is_ok());
        assert_eq!(TestEnum::One, one.unwrap());
        let two = TestEnum::try_from(2);
        assert!(two.is_ok());
        assert_eq!(TestEnum::Two, two.unwrap());
        let max = TestEnum::try_from(65535);
        assert!(max.is_ok());
        assert_eq!(TestEnum::Max, max.unwrap());
    }

    #[test]
    fn try_from_error() {
        let err = TestEnum::try_from(5);
        assert_eq!(Some(Error::OutOfRange), err.err());
    }

    #[test]
    fn into_rawtype() {
        let raw = u16::from(&TestEnum::One);
        assert_eq!(1, raw);
        let raw = u16::from(&TestEnum::Two);
        assert_eq!(2, raw);
        let raw = u16::from(&TestEnum::Max);
        assert_eq!(65535, raw);
    }
}
