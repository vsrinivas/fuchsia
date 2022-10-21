// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module holding different kinds of files and their building blocks.
use {
    crate::directory::entry::DirectoryEntry,
    async_trait::async_trait,
    fidl_fuchsia_io as fio,
    fuchsia_zircon::{self as zx, Status},
};

/// File nodes backed by VMOs.
pub mod vmo;

pub mod test_utils;

mod common;

pub mod connection;

/// Trait used for all files.
#[async_trait]
pub trait File: Send + Sync {
    /// Called when the file is going to be accessed, typically by a new connection.
    /// Flags is the same as the flags passed to `fidl_fuchsia_io.Node/Open`.
    /// The following flags are handled by the connection and do not need to be handled inside
    /// open():
    /// * OPEN_FLAG_TRUNCATE - A call to truncate() will be made immediately after open().
    /// * OPEN_FLAG_DESCRIBE - The OnOpen event is sent before any other requests are received from
    /// the file's client.
    async fn open(&self, flags: fio::OpenFlags) -> Result<(), Status>;

    /// Truncate the file to |length|.
    /// If there are pending attributes to update (see set_attrs), they should also be flushed at
    /// this time.  Otherwise, no attributes should be updated, other than size as needed.
    async fn truncate(&self, length: u64) -> Result<(), Status>;

    /// Get a VMO representing this file.
    /// If not supported by the underlying filesystem, should return Error(NOT_SUPPORTED).
    async fn get_backing_memory(&self, flags: fio::VmoFlags) -> Result<zx::Vmo, Status>;

    /// Get the size of this file.
    /// This is used to calculate seek offset relative to the end.
    async fn get_size(&self) -> Result<u64, Status>;

    /// Get this file's attributes.
    async fn get_attrs(&self) -> Result<fio::NodeAttributes, Status>;

    /// Set the attributes of this file based on the values in `attrs`.
    async fn set_attrs(
        &self,
        flags: fio::NodeAttributeFlags,
        attrs: fio::NodeAttributes,
    ) -> Result<(), Status>;

    /// Called when the file is closed.
    /// This function will also do the equivalent of sync() before the returning.
    async fn close(&self) -> Result<(), Status>;

    /// Sync this file's contents to the storage medium (probably disk).
    /// This does not necessarily guarantee that the file will be completely written to disk once
    /// the call returns. It merely guarantees that any changes to the file have been propagated
    /// to the next layer in the storage stack.
    async fn sync(&self) -> Result<(), Status>;

    /// Returns information about the filesystem.
    fn query_filesystem(&self) -> Result<fio::FilesystemInfo, Status> {
        Err(Status::NOT_SUPPORTED)
    }
}

// Trait for handling reads and writes to a file. Files that support Streams should handle reads and
// writes via a Pager instead of implementing this trait.
#[async_trait]
pub trait FileIo: Send + Sync {
    /// Read at most |buffer.len()| bytes starting at |offset| into |buffer|. The function may read
    /// less than |count| bytes and still return success, in which case read_at returns the number
    /// of bytes read into |buffer|.
    async fn read_at(&self, offset: u64, buffer: &mut [u8]) -> Result<u64, Status>;

    /// Write |content| starting at |offset|, returning the number of bytes that were successfully
    /// written.
    /// If there are pending attributes to update (see set_attrs), they should also be flushed at
    /// this time.  Otherwise, no attributes should be updated, other than size as needed.
    async fn write_at(&self, offset: u64, content: &[u8]) -> Result<u64, Status>;

    /// Appends |content| returning, if successful, the number of bytes written, and the file offset
    /// after writing.  Implementations should make the writes atomic, so in the event that multiple
    /// requests to append are in-flight, it should appear that the two writes are applied in
    /// sequence.
    /// If there are pending attributes to update (see set_attrs), they should also be flushed at
    /// this time.  Otherwise, no attributes should be updated, other than size as needed.
    async fn append(&self, content: &[u8]) -> Result<(u64, u64), Status>;
}
