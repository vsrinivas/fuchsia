// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `EntryContainer` is a trait implemented by directories that allow manipulation of their
//! content.

use crate::{
    directory::{dirents_sink, entry::DirectoryEntry},
    execution_scope::ExecutionScope,
};

use {fuchsia_async::Channel, fuchsia_zircon::Status, futures::future::BoxFuture, std::sync::Arc};

pub type ReadDirentsResult = Result<Box<dyn dirents_sink::Sealed>, Status>;

pub enum AsyncReadDirents {
    Immediate(ReadDirentsResult),
    Future(BoxFuture<'static, ReadDirentsResult>),
}

impl From<Box<dyn dirents_sink::Sealed>> for AsyncReadDirents {
    fn from(done: Box<dyn dirents_sink::Sealed>) -> AsyncReadDirents {
        AsyncReadDirents::Immediate(Ok(done))
    }
}

/// This trait indicates that a directory allows it's entries to be added and removed.  Server side
/// code can use these methods to modify directory entries.  For example, `Simple` directories
/// implement this trait.  Mutable connections will also use this trait to change directory content
/// when receiving requests over the fuchsia-io FIDL protocol.
pub trait DirectlyMutable: Send + Sync {
    /// Adds a child entry to this directory.
    ///
    /// Possible errors are:
    ///   * `name` exceeding [`MAX_FILENAME`] bytes in length.
    ///   * An entry with the same name is already present in the directory.
    fn add_entry<Name>(
        self: Arc<Self>,
        name: Name,
        entry: Arc<dyn DirectoryEntry>,
    ) -> Result<(), Status>
    where
        Name: Into<String>,
        Self: Sized,
    {
        self.add_entry_impl(name.into(), entry)
    }

    /// Adds a child entry to this directory.
    ///
    /// Possible errors are:
    ///   * `name` exceeding [`MAX_FILENAME`] bytes in length.
    ///   * An entry with the same name is already present in the directory.
    fn add_entry_impl(
        self: Arc<Self>,
        name: String,
        entry: Arc<dyn DirectoryEntry>,
    ) -> Result<(), Status>;

    /// Removes a child entry from this directory.  In case an entry with the matching name was
    /// found, the entry will be returned to the caller.
    ///
    /// Possible errors are:
    ///   * `name` exceeding [`MAX_FILENAME`] bytes in length.
    fn remove_entry<Name>(
        self: Arc<Self>,
        name: Name,
    ) -> Result<Option<Arc<dyn DirectoryEntry>>, Status>
    where
        Name: Into<String>,
        Self: Sized,
    {
        self.remove_entry_impl(name.into())
    }

    /// Removes a child entry from this directory.  In case an entry with the matching name was
    /// found, the entry will be returned to the caller.
    ///
    /// Possible errors are:
    ///   * `name` exceeding [`MAX_FILENAME`] bytes in length.
    fn remove_entry_impl(
        self: Arc<Self>,
        name: String,
    ) -> Result<Option<Arc<dyn DirectoryEntry>>, Status>;
}

/// All directories that allow inspection of their entries implement this trait.
pub trait Observable<TraversalPosition>: Send + Sync
where
    TraversalPosition: Default + Send + Sync + 'static,
{
    fn read_dirents(
        self: Arc<Self>,
        pos: TraversalPosition,
        sink: Box<dyn dirents_sink::Sink<TraversalPosition>>,
    ) -> AsyncReadDirents;

    fn register_watcher(
        self: Arc<Self>,
        scope: ExecutionScope,
        mask: u32,
        channel: Channel,
    ) -> Status;

    fn unregister_watcher(self: Arc<Self>, key: usize);
}
