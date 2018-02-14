// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Error (common to all fidl operations)

use CloseChannel;
use std::io;
use std::result;

use zircon;

/// A specialized `Result` type for FIDL operations.
pub type Result<T> = result::Result<T, Error>;

/// The error type used by FIDL operations.
#[derive(Fail, Debug)]
pub enum Error {
    /// FIDL out-of-line data was stored with bad alignment.
    #[fail(display = "FIDL out-of-line data was stored with bad alignment")]
    BadAlignment,

    /// Invalid header for a FIDL buffer.
    #[fail(display = "Invalid header for a FIDL buffer.")]
    InvalidHeader,

    /// Invalid FIDL buffer.
    #[fail(display = "Invalid FIDL buffer.")]
    Invalid,

    /// The FIDL object could not fit within the provided buffer range.
    #[fail(display = "The FIDL object could not fit within the provided buffer range")]
    OutOfRange,

    /// The FIDL object had too many layers of structural recursion.
    #[fail(display = "The FIDL object had too many layers of structural recursion.")]
    MaxRecursionDepth,

    /// There was an attempt read or write a null-valued object as a non-nullable type.
    #[fail(display = "There was an attempt to read or write a null-valued object as a non-nullable FIDL type.")]
    NotNullable,

    /// Incorrectly encoded UTF8.
    #[fail(display = "A FIDL message contained incorrectly encoded UTF8.")]
    Utf8Error,

    /// There was an attempt to decode a FIDL message containing an invalid handle.
    #[fail(display = "There was an attempt to decode a FIDL message containing an invalid handle.")]
    InvalidHandle,

    /// A message was recieved for an ordinal value that the service does not understand.
    /// This generally results from an attempt to call a FIDL service of a type other than
    /// the one being served.
    #[fail(display ="A message was received for ordinal value {} \
                     that the FIDL service {} does not understand.", ordinal, service_name)]
    UnknownOrdinal {
        /// The unknown ordinal.
        ordinal: u32,
        /// The name of the service for which the message was intented.
        service_name: &'static str,
    },

    /// Unrecognized descriminant for a FIDL union type.
    #[fail(display = "Unrecognized descriminant for a FIDL union type.")]
    UnknownUnionTag,

    /// A future was polled after it had already completed.
    #[fail(display = "A FIDL future was polled after it had already completed.")]
    PollAfterCompletion,

    /// A FIDL server encountered an IO error writing a response to a channel.
    #[fail(display = "A server encountered an IO error writing a FIDL response to a channel: {}", _0)]
    ServerResponseWrite(#[cause] io::Error),

    /// A FIDL server encountered an IO error reading incoming requests from a channel.
    #[fail(display =
          "A FIDL server encountered an IO error reading incoming FIDL requests from a channel: {}", _0)]
    ServerRequestRead(#[cause] io::Error),

    /// A FIDL client encountered an IO error reading a response from a channel.
    #[fail(display = "A FIDL client encountered an IO error reading a response from a channel: {}", _0)]
    ClientRead(#[cause] io::Error),

    /// A FIDL client encountered an IO error writing a request to a channel.
    #[fail(display = "A FIDL client encountered an IO error writing a request into a channel: {}", _0)]
    ClientWrite(#[cause] io::Error),

    /// There was an error creating a channel to be used for a FIDL client-server pair.
    #[fail(display = "There was an error creating a channel to be used for a FIDL client-server pair: {}", _0)]
    ChannelPairCreate(#[cause] zircon::Status),

    /// There was an error attaching a FIDL channel to the Tokio reactor.
    #[fail(display = "There was an error attaching a FIDL channel to the Tokio reactor: {}", _0)]
    AsyncChannel(#[cause] io::Error),

    /// There was a miscellaneous io::Error during a test.
    #[cfg(test)]
    #[fail(display = "Test io::Error: {}", _0)]
    TestIo(#[cause] io::Error),

    #[doc(hidden)]
    #[fail(display = "__Nonexhaustive error should never be created.")]
    __Nonexhaustive,
}

/// The result of an attempt to run a FIDL request handler and serialize the result.
/// This should only be used when implementing the `Stub` trait.
#[derive(Debug)]
pub enum ErrorOrClose {
    /// A FIDL error.
    Error(Error),
    /// A signal indicating that an error occurred in a request handler and
    /// the server's channel should be closed.
    CloseChannel,
}

impl From<Error> for ErrorOrClose {
    fn from(err: Error) -> Self {
        ErrorOrClose::Error(err)
    }
}

impl From<CloseChannel> for ErrorOrClose {
    fn from(_: CloseChannel) -> Self {
        ErrorOrClose::CloseChannel
    }
}
