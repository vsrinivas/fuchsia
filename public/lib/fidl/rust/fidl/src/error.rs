// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Error (common to all fidl operations)

use std::io;
use std::result;
use zircon;

/// A specialized `Result` type for FIDL operations.
pub type Result<T> = result::Result<T, Error>;

/// The error type used by FIDL operations.
#[derive(Debug)]
pub enum Error {
    /// The startup handle on which the FIDL service attempted to run was missing.
    MissingStartupHandle,

    /// Invalid header for a FIDL buffer.
    InvalidHeader,

    /// Invalid FIDL buffer.
    Invalid,

    /// The FIDL object could not fit within the provided buffer range.
    OutOfRange,

    /// There was an attempt read or write a null-valued object as a non-nullable type.
    NotNullable,

    /// Incorrectly encoded UTF8.
    Utf8Error,

    /// The provided handle was invalid.
    InvalidHandle,

    /// A message was recieved for an ordinal value that the service does not understand.
    /// This generally results from an attempt to call a FIDL service of a type other than
    /// the one being served.
    UnknownOrdinal,

    /// Unrecognized descriminant for a FIDL union type.
    UnknownUnionTag,

    /// The remote handle was closed.
    RemoteClosed,

    /// A future was polled after it had already completed.
    PollAfterCompletion,

    /// An error was encountered during the execution of the server-side handler.
    ServerExecution,

    /// An IO error.
    IoError(io::Error),

    #[doc(hidden)]
    __Nonexhaustive,
}

impl From<io::Error> for Error {
    fn from(error: io::Error) -> Error {
        Error::IoError(error)
    }
}

impl From<zircon::Status> for Error {
    fn from(status: zircon::Status) -> Error {
        Error::from(io::Error::from(status))
    }
}
