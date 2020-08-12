// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module holding different kinds of files and their building blocks.
use {
    crate::directory::entry::DirectoryEntry, async_trait::async_trait,
    fidl_fuchsia_io::NodeAttributes, fidl_fuchsia_mem::Buffer, fuchsia_zircon::Status,
};

/// File nodes with per-connection buffers.
pub mod pcb;

/// File nodes backed by VMOs.
pub mod vmo;

pub mod test_utils;

mod common;

pub mod connection;

/// Trait used for all files.
#[async_trait]
pub trait File: Sync + Send + DirectoryEntry {
    /// Called when the file is going to be accessed, typically by a new connection.
    /// Flags is the same as the flags passed to `fidl_fuchsia_io.Node/Open`.
    /// The following flags are handled by the connection and do not need to be handled inside
    /// open():
    /// * OPEN_FLAG_TRUNCATE - A call to truncate() will be made immediately after open().
    /// * OPEN_FLAG_DESCRIBE - The OnOpen event is sent before any other requests are received from
    /// the file's client.
    async fn open(&self, flags: u32) -> Result<(), Status>;

    /// Read at most |count| bytes starting at |offset|, returning a vector of the read bytes.
    async fn read_at(&self, offset: u64, count: u64) -> Result<Vec<u8>, Status>;

    /// Write |content| starting at |offset|, returning the number of bytes that were successfully
    /// written.
    async fn write_at(&self, offset: u64, content: &[u8]) -> Result<u64, Status>;

    /// Appends |content| returning, if successful, the number of bytes written, and the file offset
    /// after writing.  Implementations should make the writes atomic, so in the event that multiple
    /// requests to append are in-flight, it should appear that the two writes are applied in
    /// sequence.
    async fn append(&self, content: &[u8]) -> Result<(u64, u64), Status>;

    /// Truncate the file to |length|.
    async fn truncate(&self, length: u64) -> Result<(), Status>;

    /// Get a VMO representing this file.
    /// If this is not supported by the underlying filesystem, Ok(None) should be returned.
    async fn get_buffer(&self, mode: SharingMode, flags: u32) -> Result<Option<Buffer>, Status>;

    /// Get the size of this file.
    /// This is used to calculate seek offset relative to the end.
    async fn get_size(&self) -> Result<u64, Status>;

    /// Get this file's attributes.
    /// The "mode" field will be filled in by the connection.
    async fn get_attrs(&self) -> Result<NodeAttributes, Status>;

    /// Set the attributes of this file based on the values in `attrs`.
    /// The attributes to update are specified in flags, see fidl_fuchsia_io::NODE_ATTRIBUTE_FLAG_*.
    async fn set_attrs(&self, flags: u32, attrs: NodeAttributes) -> Result<(), Status>;

    /// Called when the file is closed.
    /// This function will also do the equivalent of sync() before the returning.
    async fn close(&self) -> Result<(), Status>;

    /// Sync this file's contents to the storage medium (probably disk).
    /// This does not necessarily guarantee that the file will be completely written to disk once
    /// the call returns. It merely guarantees that any changes to the file have been propagated
    /// to the next layer in the storage stack.
    async fn sync(&self) -> Result<(), Status>;
}

/// VMO mode for get_buffer.
#[derive(Debug, PartialEq)]
pub enum SharingMode {
    Shared,
    Private,
}
