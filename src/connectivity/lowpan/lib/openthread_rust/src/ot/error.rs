// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use num::{FromPrimitive, ToPrimitive};
use num_derive::{FromPrimitive, ToPrimitive};
use openthread_sys::*;

/// Type returned by OpenThread calls.
pub type Result<T = (), E = Error> = std::result::Result<T, E>;

/// Error type for when a given channel index is out of range.
#[derive(Debug, Copy, Clone, Eq, PartialEq, thiserror::Error)]
pub struct ChannelOutOfRange;

impl std::fmt::Display for ChannelOutOfRange {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        std::fmt::Debug::fmt(self, f)
    }
}

/// Error type for when a slice is not the correct size.
#[derive(Debug, Copy, Clone, Eq, PartialEq, thiserror::Error)]
pub struct WrongSize;

impl std::fmt::Display for WrongSize {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        std::fmt::Debug::fmt(self, f)
    }
}

/// Error type for when there are no buffers to allocate.
#[derive(Debug, Copy, Clone, Eq, PartialEq, thiserror::Error)]
pub struct NoBufs;

impl std::fmt::Display for NoBufs {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        std::fmt::Debug::fmt(self, f)
    }
}

impl From<NoBufs> for Error {
    fn from(_: NoBufs) -> Self {
        Error::NoBufs
    }
}

/// Error type for when an IPv6 header is malformed or there are no buffers to allocate.
#[derive(Debug, Copy, Clone, Eq, PartialEq, thiserror::Error)]
pub struct MalformedOrNoBufs;

impl std::fmt::Display for MalformedOrNoBufs {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        std::fmt::Debug::fmt(self, f)
    }
}

/// Error type for OpenThread calls. Functional equivalent of
/// [`otsys::otError`](crate::otsys::otError).
#[derive(
    Debug, Copy, Clone, Eq, Ord, PartialOrd, PartialEq, FromPrimitive, ToPrimitive, thiserror::Error,
)]
#[allow(missing_docs)]
pub enum Error {
    Abort = otError_OT_ERROR_ABORT as isize,
    AddressFiltered = otError_OT_ERROR_ADDRESS_FILTERED as isize,
    AddressQuery = otError_OT_ERROR_ADDRESS_QUERY as isize,
    Already = otError_OT_ERROR_ALREADY as isize,
    Busy = otError_OT_ERROR_BUSY as isize,
    ChannelAccessFailure = otError_OT_ERROR_CHANNEL_ACCESS_FAILURE as isize,
    DestinationAddressFiltered = otError_OT_ERROR_DESTINATION_ADDRESS_FILTERED as isize,
    Detached = otError_OT_ERROR_DETACHED as isize,
    MessageDropped = otError_OT_ERROR_DROP as isize,
    Duplicated = otError_OT_ERROR_DUPLICATED as isize,
    Failed = otError_OT_ERROR_FAILED as isize,
    Fcs = otError_OT_ERROR_FCS as isize,
    Rejected = otError_OT_ERROR_REJECTED as isize,
    Generic = otError_OT_ERROR_GENERIC as isize,
    InvalidArgs = otError_OT_ERROR_INVALID_ARGS as isize,
    InvalidCommand = otError_OT_ERROR_INVALID_COMMAND as isize,
    InvalidState = otError_OT_ERROR_INVALID_STATE as isize,
    Ip6AddressCreationFailure = otError_OT_ERROR_IP6_ADDRESS_CREATION_FAILURE as isize,
    LinkMarginLow = otError_OT_ERROR_LINK_MARGIN_LOW as isize,
    NotCapable = otError_OT_ERROR_NOT_CAPABLE as isize,
    NotFound = otError_OT_ERROR_NOT_FOUND as isize,
    NotImplemented = otError_OT_ERROR_NOT_IMPLEMENTED as isize,
    NotLowpanDataFrame = otError_OT_ERROR_NOT_LOWPAN_DATA_FRAME as isize,
    None = otError_OT_ERROR_NONE as isize,
    NotTmf = otError_OT_ERROR_NOT_TMF as isize,
    NoAck = otError_OT_ERROR_NO_ACK as isize,
    NoAddress = otError_OT_ERROR_NO_ADDRESS as isize,
    NoBufs = otError_OT_ERROR_NO_BUFS as isize,
    NoFrameReceived = otError_OT_ERROR_NO_FRAME_RECEIVED as isize,
    NoRoute = otError_OT_ERROR_NO_ROUTE as isize,
    Parse = otError_OT_ERROR_PARSE as isize,
    Pending = otError_OT_ERROR_PENDING as isize,
    ReassemblyTimeout = otError_OT_ERROR_REASSEMBLY_TIMEOUT as isize,
    ResponseTimeout = otError_OT_ERROR_RESPONSE_TIMEOUT as isize,
    Security = otError_OT_ERROR_SECURITY as isize,
    UnknownNeighbor = otError_OT_ERROR_UNKNOWN_NEIGHBOR as isize,
}

impl Error {
    /// Converts this [`ot::Error`](crate::ot::Error) into a
    /// [`otsys::otError`](crate::otsys::otError).
    pub fn into_ot_error(self) -> otError {
        self.to_u32().unwrap()
    }

    /// Converts this [`ot::Error`](crate::ot::Error) into a [`ot::Result`](crate::ot::Result),
    /// mapping [`ot::Error::None`](crate::ot::Error::None) to [`Ok(())`] and any other error to
    /// [`Err(x)`].
    pub fn into_result(self) -> Result {
        if self == Self::None {
            Ok(())
        } else {
            Err(self)
        }
    }
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Debug::fmt(self, f)
    }
}

/// Trait for converting types into `otError` values.
pub trait IntoOtError {
    /// Converts this value into a
    /// [`otsys::otError`](crate::otsys::otError).
    fn into_ot_error(self) -> otError;
}

impl IntoOtError for Error {
    fn into_ot_error(self) -> otError {
        self.to_u32().unwrap()
    }
}

impl IntoOtError for Result<(), Error> {
    fn into_ot_error(self) -> otError {
        self.err().unwrap_or(Error::None).into_ot_error()
    }
}

impl From<Result<(), Error>> for Error {
    fn from(result: Result<(), Error>) -> Self {
        match result {
            Ok(()) => Error::None,
            Err(e) => e,
        }
    }
}

impl From<otError> for Error {
    fn from(err: otError) -> Self {
        Error::from_u32(err).expect(format!("Unknown otError value: {}", err).as_str())
    }
}

impl From<()> for Error {
    fn from(_: ()) -> Self {
        Error::None
    }
}

impl From<Error> for otError {
    fn from(err: Error) -> Self {
        err.into_ot_error()
    }
}

impl Into<Result> for Error {
    fn into(self) -> Result {
        self.into_result()
    }
}
