// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `EntryContainer` is a trait implemented by directories that allow manipulation of their
//! content.

use crate::{
    directory::{dirents_sink, entry::DirectoryEntry, traversal_position::TraversalPosition},
    execution_scope::ExecutionScope,
    filesystem::Filesystem,
    path::Path,
};

use {
    async_trait::async_trait,
    fidl_fuchsia_io::NodeAttributes,
    fuchsia_async::Channel,
    fuchsia_zircon::Status,
    futures::future::BoxFuture,
    std::{any::Any, sync::Arc},
};

pub type GetEntryResult = Result<Arc<dyn DirectoryEntry>, Status>;

pub enum AsyncGetEntry {
    Immediate(GetEntryResult),
    Future(BoxFuture<'static, GetEntryResult>),
}

impl From<Status> for AsyncGetEntry {
    fn from(status: Status) -> AsyncGetEntry {
        AsyncGetEntry::Immediate(Err(status))
    }
}

impl From<Arc<dyn DirectoryEntry>> for AsyncGetEntry {
    fn from(entry: Arc<dyn DirectoryEntry>) -> AsyncGetEntry {
        AsyncGetEntry::Immediate(Ok(entry))
    }
}

impl From<BoxFuture<'static, GetEntryResult>> for AsyncGetEntry {
    fn from(future: BoxFuture<'static, GetEntryResult>) -> AsyncGetEntry {
        AsyncGetEntry::Future(future)
    }
}

/// All directories implement this trait.  If a directory can be modified it should
/// also implement the `MutableDirectory` trait.
#[async_trait]
pub trait Directory: Any + Send + Sync {
    /// Returns a reference to a contained directory entry.  Used when linking entries.
    fn get_entry(self: Arc<Self>, name: String) -> AsyncGetEntry;

    /// Reads directory entries starting from `pos` by adding them to `sink`.
    /// Once finished, should return a sealed sink.
    // The lifetimes here are because of https://github.com/rust-lang/rust/issues/63033.
    async fn read_dirents<'a>(
        &'a self,
        pos: &'a TraversalPosition,
        sink: Box<dyn dirents_sink::Sink>,
    ) -> Result<(TraversalPosition, Box<dyn dirents_sink::Sealed>), Status>;

    /// Register a watcher for this directory.
    /// Implementations will probably want to use a `Watcher` to manage watchers.
    fn register_watcher(
        self: Arc<Self>,
        scope: ExecutionScope,
        mask: u32,
        channel: Channel,
    ) -> Result<(), Status>;

    /// Unregister a watcher from this directory. The watcher should no longer
    /// receive events.
    fn unregister_watcher(self: Arc<Self>, key: usize);

    /// Get this directory's attributes.
    /// The "mode" field will be filled in by the connection.
    fn get_attrs(&self) -> Result<NodeAttributes, Status>;
}

/// This trait indicates a directory that can be mutated by adding and removing entries.
/// This trait must be implemented to use a `MutableConnection`, however, a directory could also
/// implement the `DirectlyMutable` type, which provides a blanket implementation of this trait.
pub trait MutableDirectory: Directory {
    /// Adds a child entry to this directory, even if it already exists.  The target is discarded
    /// if it exists.
    fn link(&self, name: String, entry: Arc<dyn DirectoryEntry>) -> Result<(), Status>;

    /// Set the attributes of this directory based on the values in `attrs`.
    /// The attributes to update are specified in flags, see fidl_fuchsia_io::NODE_ATTRIBUTE_FLAG_*.
    fn set_attrs(&self, flags: u32, attributes: NodeAttributes) -> Result<(), Status>;

    /// Removes an entry from this directory.
    fn unlink(&self, name: Path) -> Result<(), Status>;

    /// Gets the filesystem this directory belongs to.
    fn get_filesystem(&self) -> Arc<dyn Filesystem>;

    /// Gets this directory as an Any.
    fn into_any(self: Arc<Self>) -> Arc<dyn Any + Send + Sync>;

    /// Syncs the directory.
    fn sync(&self) -> Result<(), Status>;
}
