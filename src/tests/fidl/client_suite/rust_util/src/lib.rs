// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fidl_clientsuite::FidlErrorKind;

/// Classify a [`fidl::Error`] as a [`FidlErrorKind`] that can be returned in a
/// dynsuite client test.
pub fn classify_error(error: fidl::Error) -> FidlErrorKind {
    match error {
        fidl::Error::InvalidBoolean
        | fidl::Error::InvalidHeader
        | fidl::Error::IncompatibleMagicNumber(_)
        | fidl::Error::Invalid
        | fidl::Error::OutOfRange
        | fidl::Error::OverflowIncorrectHandleCount
        | fidl::Error::OverflowControlPlaneBodyNotEmpty
        | fidl::Error::ExtraBytes
        | fidl::Error::ExtraHandles
        | fidl::Error::NonZeroPadding { .. }
        | fidl::Error::MaxRecursionDepth
        | fidl::Error::NotNullable
        | fidl::Error::UnexpectedNullRef
        | fidl::Error::Utf8Error
        | fidl::Error::InvalidBitsValue
        | fidl::Error::InvalidEnumValue
        | fidl::Error::UnknownUnionTag
        | fidl::Error::InvalidPresenceIndicator
        | fidl::Error::InvalidInlineBitInEnvelope
        | fidl::Error::InvalidInlineMarkerInEnvelope
        | fidl::Error::InvalidNumBytesInEnvelope
        | fidl::Error::InvalidHostHandle
        | fidl::Error::IncorrectHandleSubtype { .. }
        | fidl::Error::MissingExpectedHandleRights { .. }
        | fidl::Error::CannotStoreUnknownHandles => FidlErrorKind::DecodingError,
        fidl::Error::UnknownOrdinal { .. }
        | fidl::Error::InvalidResponseTxid
        | fidl::Error::UnexpectedSyncResponse => FidlErrorKind::UnexpectedMessage,
        fidl::Error::UnsupportedMethod { .. } => FidlErrorKind::UnknownMethod,
        fidl::Error::ClientChannelClosed { .. } => FidlErrorKind::ChannelPeerClosed,
        _ => FidlErrorKind::OtherError,
    }
}
