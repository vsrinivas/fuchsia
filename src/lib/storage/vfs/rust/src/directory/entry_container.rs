// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `EntryContainer` is a trait implemented by directories that allow manipulation of their
//! content.

use crate::{
    common::IntoAny,
    directory::{dirents_sink, traversal_position::TraversalPosition},
    execution_scope::ExecutionScope,
    filesystem::Filesystem,
};

use {
    async_trait::async_trait,
    fidl_fuchsia_io::NodeAttributes,
    fidl_fuchsia_io_admin::FilesystemInfo,
    fuchsia_async::Channel,
    fuchsia_zircon::Status,
    std::{any::Any, sync::Arc},
};

/// All directories implement this trait.  If a directory can be modified it should
/// also implement the `MutableDirectory` trait.
#[async_trait]
pub trait Directory: IntoAny + Send + Sync {
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
    async fn get_attrs(&self) -> Result<NodeAttributes, Status>;

    /// Called when the directory is closed.
    fn close(&self) -> Result<(), Status>;

    fn query_filesystem(&self) -> Result<FilesystemInfo, Status> {
        Err(Status::NOT_SUPPORTED)
    }
}

/// This trait indicates a directory that can be mutated by adding and removing entries.
/// This trait must be implemented to use a `MutableConnection`, however, a directory could also
/// implement the `DirectlyMutable` type, which provides a blanket implementation of this trait.
#[async_trait]
pub trait MutableDirectory: Directory + Send + Sync {
    /// Adds a child entry to this directory.  If the target exists, it should fail with
    /// ZX_ERR_ALREADY_EXISTS.
    async fn link(
        self: Arc<Self>,
        _name: String,
        _source_dir: Arc<dyn Any + Send + Sync>,
        _source_name: &str,
    ) -> Result<(), Status> {
        Err(Status::NOT_SUPPORTED)
    }

    /// Set the attributes of this directory based on the values in `attrs`.
    /// The attributes to update are specified in flags, see fidl_fuchsia_io::NODE_ATTRIBUTE_FLAG_*.
    async fn set_attrs(&self, flags: u32, attributes: NodeAttributes) -> Result<(), Status>;

    /// Removes an entry from this directory.
    async fn unlink(&self, name: &str, must_be_directory: bool) -> Result<(), Status>;

    /// Gets the filesystem this directory belongs to.
    fn get_filesystem(&self) -> &dyn Filesystem;

    /// Syncs the directory.
    async fn sync(&self) -> Result<(), Status>;
}
