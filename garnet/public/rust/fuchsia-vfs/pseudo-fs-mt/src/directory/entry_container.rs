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

/// All directories that contain other entries implement this trait.  Directories may implement
/// other traits such as [`DirectlyMutable`] and/or [`Observable`] as well.
pub trait EntryContainer: Send + Sync {
    /// Returns a reference to a contained directory entry.  Used when linking entries.
    fn get_entry(self: Arc<Self>, name: String) -> AsyncGetEntry;
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

    /// Very similar to [`add_entry_impl`] with the only exception that the target is overwritten
    /// even if it already exists.  The target is discarded, if it exists.
    fn link(self: Arc<Self>, name: String, entry: Arc<dyn DirectoryEntry>) -> Result<(), Status>;

    /// Renaming needs to be atomic, even accross two distinct directories.  So we need a special
    /// API to handle that.
    ///
    /// As two distinc directories may mean two mutexes to lock, using it correctly is non-trivial.
    /// In order to avoid a deadlock, we need to decide on a global ordering for the locks.
    /// `rename_from` and [`rename_to`] are used depending on the relative order of the two
    /// directories involved in the operation.
    ///
    /// This method is `unsafe`, as `rename_from` and [`rename_to`] should only be called by the
    /// [`rename_helper`], which will establish the global order and will call proper method.  It
    /// should reduce the chances one will use this API incorrectly.
    ///
    /// Implementations are expected to lock this directory, check that the entry exists and pass a
    /// reference to the entry to the `to` callback.  Only if the `to` callback succeed, should the
    /// entry be removed from the current directory.  This will garantee atomic rename from the
    /// standpoint of the client.
    unsafe fn rename_from(
        self: Arc<Self>,
        src: String,
        to: Box<dyn FnOnce(Arc<dyn DirectoryEntry>) -> Result<(), Status>>,
    ) -> Result<(), Status>;

    /// Renaming needs to be atomic, even accross two distinct directories.  So we need a special
    /// API to handle that.
    ///
    /// See [`rename_from`] comment for an explanation.
    ///
    /// Implementations are expected to lock this dirctory, check if they can accept an entry named
    /// `dst` (in case there might be any restrictions), then call the `from` callback to obtain a
    /// new entry which must be added into the current directory with no errors.
    unsafe fn rename_to(
        self: Arc<Self>,
        dst: String,
        from: Box<dyn FnOnce() -> Result<Arc<dyn DirectoryEntry>, Status>>,
    ) -> Result<(), Status>;

    /// In case an entry is renamed within the same directory only one lock needs to be obtained.
    /// This is a companion method to the [`rename_from`]/[`rename_to`] pair.  [`rename_helper`]
    /// will use this method to avoid locking the same directory mutex twice.
    ///
    /// It should only be used by the [`rename_helper`].
    fn rename_within(self: Arc<Self>, src: String, dst: String) -> Result<(), Status>;
}

pub fn rename_helper(
    src_parent: Arc<dyn DirectlyMutable>,
    src: String,
    dst_parent: Arc<dyn DirectlyMutable>,
    dst: String,
) -> Result<(), Status> {
    // We need to lock directories using the same global order, otherwise we risk a deadlock.  We
    // will use directory objects memory location to establish global order for the locks.  It
    // introduces additional complexity, but, hopefully, avoids this subtle deadlocking issue.
    //
    // We will lock first object with the smaller memory address.

    let src_order = src_parent.as_ref() as *const dyn DirectlyMutable as *const usize as usize;
    let dst_order = dst_parent.as_ref() as *const dyn DirectlyMutable as *const usize as usize;

    if src_order < dst_order {
        // `unsafe` here indicates that we have checked the global order for the locks for
        // `src_parent` and `dst_parent` and we are calling `rename_from` as `src_parent` has a
        // smaller memory address than the `dst_parent`.
        unsafe { src_parent.rename_from(src, Box::new(move |entry| dst_parent.link(dst, entry))) }
    } else if src_order == dst_order {
        src_parent.rename_within(src, dst)
    } else {
        // `unsafe` here indicates that we have checked the global order for the locks for
        // `src_parent` and `dst_parent` and we are calling `rename_to` as `dst_parent` has a
        // smaller memory address than the `src_parent`.
        unsafe {
            dst_parent.rename_to(
                dst,
                Box::new(move || match src_parent.remove_entry_impl(src)? {
                    None => Err(Status::NOT_FOUND),
                    Some(entry) => Ok(entry),
                }),
            )
        }
    }
}

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
