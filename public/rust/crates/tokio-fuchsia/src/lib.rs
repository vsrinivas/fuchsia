// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A wrapper to expose Zircon kernel objects for use in tokio.

#[macro_use]
extern crate futures;
#[macro_use]
extern crate tokio_core;
extern crate mio;
extern crate fuchsia_zircon as zircon;

mod channel;

pub use channel::Channel;

use std::io;

fn would_block() -> io::Error {
    io::Error::new(io::ErrorKind::WouldBlock, "would block")
}

/// Convert from zircon::Status to io::Error.
///
/// Note: these conversions are done on a "best-effort" basis and may not necessarily reflect
/// exactly equivalent error types.
fn status_to_io_err(status: zircon::Status) -> io::Error {
    use zircon::Status;

    let err_kind: io::ErrorKind = match status {
        Status::ErrInterruptedRetry => io::ErrorKind::Interrupted,
        Status::ErrBadHandle => io::ErrorKind::BrokenPipe,
        Status::ErrTimedOut => io::ErrorKind::TimedOut,
        Status::ErrShouldWait => io::ErrorKind::WouldBlock,
        Status::ErrPeerClosed => io::ErrorKind::ConnectionAborted,
        Status::ErrNotFound => io::ErrorKind::NotFound,
        Status::ErrAlreadyExists => io::ErrorKind::AlreadyExists,
        Status::ErrAlreadyBound => io::ErrorKind::AddrInUse,
        Status::ErrUnavailable => io::ErrorKind::AddrNotAvailable,
        Status::ErrAccessDenied => io::ErrorKind::PermissionDenied,
        Status::ErrIoRefused => io::ErrorKind::ConnectionRefused,
        Status::ErrIoDataIntegrity => io::ErrorKind::InvalidData,

        Status::ErrBadPath |
        Status::ErrInvalidArgs |
        Status::ErrOutOfRange |
        Status::ErrWrongType => io::ErrorKind::InvalidInput,

        Status::UnknownOther |
        Status::ErrNext |
        Status::ErrStop |
        Status::ErrNoSpace |
        Status::ErrFileBig |
        Status::ErrNotFile |
        Status::ErrNotDir |
        Status::ErrIoDataLoss |
        Status::ErrIo |
        Status::ErrCanceled |
        Status::ErrBadState |
        Status::ErrBufferTooSmall |
        Status::ErrBadSyscall |
        Status::NoError |
        Status::ErrInternal |
        Status::ErrNotSupported |
        Status::ErrNoResources |
        Status::ErrNoMemory |
        Status::ErrCallFailed
        => io::ErrorKind::Other
    }.into();

    err_kind.into()
}
