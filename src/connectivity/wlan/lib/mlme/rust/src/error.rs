// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::ScanError,
    failure::{self, Fail},
    fuchsia_zircon as zx,
    wlan_common::{
        appendable::BufferTooSmall,
        error::{FrameParseError, FrameWriteError},
    },
};

#[derive(Debug, Fail)]
pub enum Error {
    #[fail(display = "out of buffers; requested {} bytes", _0)]
    NoResources(usize),
    #[fail(display = "provided buffer to small")]
    BufferTooSmall,
    #[fail(display = "error parsing frame: {}", _0)]
    ParsingFrame(#[cause] FrameParseError),
    #[fail(display = "error writing frame: {}", _0)]
    WritingFrame(#[cause] FrameWriteError),
    #[fail(display = "scan error: {}", _0)]
    ScanError(#[cause] ScanError),
    #[fail(display = "{}", _0)]
    Internal(#[cause] failure::Error),
    #[fail(display = "{}", _0)]
    Fidl(#[cause] fidl::Error),
    #[fail(display = "{}; {}", _0, _1)]
    Status(String, #[cause] zx::Status),
}

impl From<Error> for zx::Status {
    fn from(e: Error) -> Self {
        match e {
            Error::NoResources(_) => zx::Status::NO_RESOURCES,
            Error::BufferTooSmall => zx::Status::BUFFER_TOO_SMALL,
            Error::Internal(_) => zx::Status::INTERNAL,
            Error::ParsingFrame(_) => zx::Status::IO_INVALID,
            Error::WritingFrame(_) => zx::Status::IO_REFUSED,
            Error::ScanError(e) => e.into(),
            Error::Fidl(e) => match e {
                fidl::Error::ClientRead(status)
                | fidl::Error::ClientWrite(status)
                | fidl::Error::ServerResponseWrite(status)
                | fidl::Error::ServerRequestRead(status)
                | fidl::Error::ChannelPairCreate(status) => status,
                _ => zx::Status::IO,
            },
            Error::Status(_, status) => status,
        }
    }
}

pub trait ResultExt {
    /// Returns ZX_OK if Self is Ok, otherwise, prints an error and turns Self into a corresponding
    /// ZX_ERR_*.
    fn into_raw_zx_status(self) -> zx::sys::zx_status_t;
}

impl ResultExt for Result<(), Error> {
    fn into_raw_zx_status(self) -> zx::sys::zx_status_t {
        match self {
            Ok(()) | Err(Error::Status(_, zx::Status::OK)) => zx::sys::ZX_OK,
            Err(e) => {
                eprintln!("{}", e);
                Into::<zx::Status>::into(e).into_raw()
            }
        }
    }
}

impl From<failure::Error> for Error {
    fn from(e: failure::Error) -> Self {
        Error::Internal(e)
    }
}

impl From<FrameParseError> for Error {
    fn from(e: FrameParseError) -> Self {
        Error::ParsingFrame(e)
    }
}

impl From<FrameWriteError> for Error {
    fn from(e: FrameWriteError) -> Self {
        Error::WritingFrame(e)
    }
}

impl From<ScanError> for Error {
    fn from(e: ScanError) -> Self {
        Error::ScanError(e)
    }
}

impl From<BufferTooSmall> for Error {
    fn from(_: BufferTooSmall) -> Self {
        Error::BufferTooSmall
    }
}

impl From<fidl::Error> for Error {
    fn from(e: fidl::Error) -> Self {
        Error::Fidl(e)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, failure::format_err};

    #[test]
    fn test_error_into_status() {
        let status = zx::Status::from(Error::Status("foo".to_string(), zx::Status::OK));
        assert_eq!(status, zx::Status::OK);

        let status = zx::Status::from(Error::Status("foo".to_string(), zx::Status::NOT_SUPPORTED));
        assert_eq!(status, zx::Status::NOT_SUPPORTED);

        let status = zx::Status::from(Error::Internal(format_err!("lorem")));
        assert_eq!(status, zx::Status::INTERNAL);

        let status = zx::Status::from(Error::WritingFrame(FrameWriteError::BufferTooSmall));
        assert_eq!(status, zx::Status::IO_REFUSED);

        let status = zx::Status::from(Error::BufferTooSmall);
        assert_eq!(status, zx::Status::BUFFER_TOO_SMALL);

        let status = zx::Status::from(Error::NoResources(42));
        assert_eq!(status, zx::Status::NO_RESOURCES);

        let status =
            zx::Status::from(Error::Fidl(fidl::Error::ClientWrite(zx::Status::NOT_SUPPORTED)));
        assert_eq!(status, zx::Status::NOT_SUPPORTED);
    }

    #[test]
    fn test_result_into_status() {
        let status = Err(Error::Status("foo".to_string(), zx::Status::OK));
        assert_eq!(status.into_raw_zx_status(), zx::sys::ZX_OK);

        let status = Err(Error::Status("foo".to_string(), zx::Status::NOT_SUPPORTED));
        assert_eq!(status.into_raw_zx_status(), zx::sys::ZX_ERR_NOT_SUPPORTED);

        let status = Err(Error::Internal(format_err!("lorem")));
        assert_eq!(status.into_raw_zx_status(), zx::sys::ZX_ERR_INTERNAL);

        let status = Ok(());
        assert_eq!(status.into_raw_zx_status(), zx::sys::ZX_OK);
    }
}
