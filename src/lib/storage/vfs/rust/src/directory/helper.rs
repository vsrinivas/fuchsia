// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        directory::{
            entry::DirectoryEntry,
            entry_container::{Directory, MutableDirectory},
        },
        filesystem::Filesystem,
        path::Path,
    },
    fidl_fuchsia_io::NodeAttributes,
    fuchsia_zircon::Status,
    std::{any::Any, sync::Arc},
};

/// `DirectlyMutable` is a superset of `MutableDirectory` which also allows server-side management
/// of directory entries (via `add_entry` and `remove_entry`). It also provides `rename_from` and
/// `rename_to` to support global ordering in rename operations. You may wish to use
/// `filesystem::simple::SimpleFilesystem` to provide the filesystem type for this DirectlyMutable.
pub trait DirectlyMutable: Directory + Send + Sync {
    /// Adds a child entry to this directory.
    ///
    /// Possible errors are:
    ///   * `name` exceeding [`MAX_FILENAME`] bytes in length.
    ///   * An entry with the same name is already present in the directory.
    fn add_entry<Name>(&self, name: Name, entry: Arc<dyn DirectoryEntry>) -> Result<(), Status>
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
    fn add_entry_impl(&self, name: String, entry: Arc<dyn DirectoryEntry>) -> Result<(), Status>;

    /// Removes a child entry from this directory.  In case an entry with the matching name was
    /// found, the entry will be returned to the caller.
    ///
    /// Possible errors are:
    ///   * `name` exceeding [`MAX_FILENAME`] bytes in length.
    fn remove_entry<Name>(&self, name: Name) -> Result<Option<Arc<dyn DirectoryEntry>>, Status>
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
    fn remove_entry_impl(&self, name: String) -> Result<Option<Arc<dyn DirectoryEntry>>, Status>;

    /// Add a child entry to this directory, even if it already exists.  The target is discarded,
    /// if it exists.
    fn link(&self, name: String, entry: Arc<dyn DirectoryEntry>) -> Result<(), Status>;

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
        &self,
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
        &self,
        dst: String,
        from: Box<dyn FnOnce() -> Result<Arc<dyn DirectoryEntry>, Status>>,
    ) -> Result<(), Status>;

    /// In case an entry is renamed within the same directory only one lock needs to be obtained.
    /// This is a companion method to the [`rename_from`]/[`rename_to`] pair.  [`rename_helper`]
    /// will use this method to avoid locking the same directory mutex twice.
    ///
    /// It should only be used by the [`rename_helper`].
    fn rename_within(&self, src: String, dst: String) -> Result<(), Status>;

    /// Get the filesystem this directory belongs to.
    fn get_filesystem(&self) -> &dyn Filesystem;

    /// Turn this DirectlyMutable into an Any.
    fn into_any(self: Arc<Self>) -> Arc<Any + Send + Sync>;
}

impl<T: DirectlyMutable> MutableDirectory for T {
    fn link(&self, name: String, entry: Arc<dyn DirectoryEntry>) -> Result<(), Status> {
        (self as &dyn DirectlyMutable).link(name, entry)
    }

    fn unlink(&self, mut name: Path) -> Result<(), Status> {
        match self.remove_entry_impl(name.next().unwrap().into()) {
            Ok(Some(_)) => Ok(()),
            Ok(None) => Err(Status::NOT_FOUND),
            Err(e) => Err(e),
        }
    }

    fn set_attrs(&self, _flags: u32, _attrs: NodeAttributes) -> Result<(), Status> {
        Err(Status::NOT_SUPPORTED)
    }

    fn get_filesystem(&self) -> &dyn Filesystem {
        (self as &dyn DirectlyMutable).get_filesystem()
    }

    fn into_any(self: Arc<Self>) -> Arc<Any + Send + Sync> {
        (self as Arc<dyn DirectlyMutable>).into_any()
    }

    fn sync(&self) -> Result<(), Status> {
        Err(Status::NOT_SUPPORTED)
    }
}
