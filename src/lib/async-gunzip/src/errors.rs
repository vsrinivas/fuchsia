// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use miniz_oxide::MZError;
use std::fmt::Debug;
use std::{error, io};

/// An error encountered during [`decode`].
///
/// [`decode`]: crate::decode
#[derive(Debug, thiserror::Error)]
pub enum Error<E>
where
    E: std::error::Error + 'static,
{
    /// An error yielded by the underlying input stream
    #[error(transparent)]
    Input(E),

    #[error("gzip decode error: {0}")]
    Decode(#[from] DecodeError),
}

impl From<io::Error> for Error<io::Error> {
    fn from(e: io::Error) -> Self {
        Self::Input(e)
    }
}

/// A decoding error, due to malformed gzip data.
#[derive(Debug, thiserror::Error)]
pub enum DecodeError {
    #[error("malformed header: {0}")]
    Header(String),

    #[error("malformed footer: {0}")]
    Footer(String),

    #[error("malformed DEFLATE body. miniz_oxide error code: {}", 0 as i32)]
    Deflate(MZError),

    /// Misc. catch-all, for things like unexpected eof.
    #[error(transparent)]
    Other(io::Error),
}

impl From<MZError> for DecodeError {
    fn from(e: MZError) -> Self {
        Self::Deflate(e)
    }
}

/// Wrap an error in an `io::Error` with `ErrorKind::Other`.
///
/// See also [`try_unwrap_error`].
pub fn wrap_error<E>(inner: E) -> io::Error
where
    E: error::Error + Send + Sync + 'static,
{
    io::Error::new(io::ErrorKind::Other, inner)
}

/// Try to unwrap an inner error with `ErrorKind::Other` and the specified type.
///
/// If unsuccessful, return the original `io::Error`.
///
/// Inverse of [`wrap_error`].
pub fn try_unwrap_error<E>(outer: io::Error) -> Result<E, io::Error>
where
    E: error::Error + 'static,
{
    match (outer.kind(), outer.get_ref()) {
        (k, Some(inner)) if k == io::ErrorKind::Other && inner.is::<E>() => {
            // Consume and return the inner error.
            let inner: Box<E> = outer
                .into_inner()
                .expect("get_ref() returned Some")
                .downcast::<E>()
                .expect("is::<E>() returned true");

            Ok(*inner)
        }
        _ => Err(outer),
    }
}

impl Error<io::Error> {
    /// Convert from `Error<io::Error>` to `Error<E>`.
    ///
    /// If there is an error of type `E` nested inside an `io::Error`, un-nest it.
    pub(crate) fn unwrap_inner_error<E>(self) -> Error<E>
    where
        E: error::Error,
    {
        match self {
            Error::Input(io_err) => match try_unwrap_error::<E>(io_err) {
                Ok(inner) => Error::Input(inner),
                Err(io_err) => Error::Decode(DecodeError::Other(io_err)),
            },
            // Trivially convert Error<io::Error> to Error<E>.
            Error::Decode(d) => Error::Decode(d),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tests::MockError;

    #[test]
    fn test_wrap_error_round_trip() {
        let e1 = MockError::BadThing("oh no!".into());
        let wrapped = wrap_error(e1);
        let e2: MockError = try_unwrap_error(wrapped).unwrap();

        assert!(matches!(e2, MockError::BadThing(..)));
    }
}
