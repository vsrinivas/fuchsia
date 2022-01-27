// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_zircon as zx, futures::channel::mpsc, thiserror::Error};

use crate::controller::VsyncEvent;

/// Library error type.
#[derive(Error, Debug)]
pub enum Error {
    /// Error encountered while connecting to a display-controller device via devfs.
    #[error("could not find a display-controller device")]
    DeviceNotFound,

    /// No displays were reported by the display driver when expected.
    #[error("device did not enumerate initial displays")]
    NoDisplays,

    /// Failed to enumerate display-controller devices via devfs.
    #[error("failed to watch files in device directory")]
    VfsWatcherError,

    /// A request handling task (such as one that owns a FIDL event stream that can only be
    /// started once) was already been initiated before.
    #[error("a singleton task was already initiated")]
    AlreadyRequested,

    /// Error while allocating shared sysmem buffers.
    #[error("sysmem buffer collection allocation failed")]
    BuffersNotAllocated,

    /// Error while establishing a connection to sysmem.
    #[error("error while setting up a sysmem connection")]
    SysmemConnection,

    /// Ran out of free client-assigned identifiers.
    #[error("ran out of identifiers")]
    IdsExhausted,

    /// Wrapper for errors from FIDL bindings.
    #[error("FIDL error: {0}")]
    FidlError(#[from] fidl::Error),

    /// Wrapper for system file I/O errors.
    #[error("OS I/O error: {0}")]
    IoError(#[from] std::io::Error),

    /// Wrapper for errors from zircon syscalls.
    #[error("zircon error: {0}")]
    ZxError(#[from] zx::Status),

    /// Error that occurred while notifying vsync event listeners over an in-process async channel.
    #[error("failed to notify vsync: {0}")]
    CouldNotSendVsyncEvent(#[from] mpsc::TrySendError<VsyncEvent>),

    /// UTF-8 validation error.
    #[error("invalid UTF-8 string")]
    InvalidUtf8(#[from] std::str::Utf8Error),
}

/// Library Result type alias.
pub type Result<T> = std::result::Result<T, Error>;
