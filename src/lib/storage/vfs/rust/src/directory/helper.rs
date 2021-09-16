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
    },
    async_trait::async_trait,
    fidl_fuchsia_io::NodeAttributes,
    fuchsia_zircon::Status,
    std::sync::Arc,
};

/// `DirectlyMutable` is a superset of `MutableDirectory` which also allows server-side management
/// of directory entries (via `add_entry` and `remove_entry`). It also provides `rename_from` and
/// `rename_to` to support global ordering in rename operations. You may wish to use
/// `filesystem::simple::SimpleFilesystem` to provide the filesystem type for this DirectlyMutable.
pub trait DirectlyMutable: Directory + Send + Sync {
    /// Adds a child entry to this directory.
    ///
    /// Possible errors are:
    ///   * `ZX_ERR_INVALID_ARGS` if `name` exceeds [`fidl_fuchsia_io::MAX_FILENAME`] bytes in
    ///     length, or if `name` includes a path separator ('/') character.
    ///   * `ZX_ERR_ALREADY_EXISTS` if an entry with the same name is already present in the
    ///     directory.
    fn add_entry<Name>(&self, name: Name, entry: Arc<dyn DirectoryEntry>) -> Result<(), Status>
    where
        Name: Into<String>,
        Self: Sized,
    {
        self.add_entry_impl(name.into(), entry, false)
    }

    /// Adds a child entry to this directory.
    ///
    /// Possible errors are:
    ///   * `ZX_ERR_INVALID_ARGS` if `name` exceeds [`fidl_fuchsia_io::MAX_FILENAME`] bytes in
    ///     length, or if `name` includes a path separator ('/') character.
    ///   * `ZX_ERR_ALREADY_EXISTS` if an entry with the same name is already present in the
    ///     directory, and `overwrite` is false.
    fn add_entry_impl(
        &self,
        name: String,
        entry: Arc<dyn DirectoryEntry>,
        overwrite: bool,
    ) -> Result<(), Status>;

    /// Removes a child entry from this directory.  In case an entry with the matching name was
    /// found, the entry will be returned to the caller.  If `must_be_directory` is true, an error
    /// is returned if the entry is not a directory.
    ///
    /// Possible errors are:
    ///   * `ZX_ERR_INVALID_ARGS` if `name` exceeds [`fidl_fuchsia_io::MAX_FILENAME`] bytes in
    ///     length.
    fn remove_entry<Name>(
        &self,
        name: Name,
        must_be_directory: bool,
    ) -> Result<Option<Arc<dyn DirectoryEntry>>, Status>
    where
        Name: Into<String>,
        Self: Sized,
    {
        self.remove_entry_impl(name.into(), must_be_directory)
    }

    /// Removes a child entry from this directory.  In case an entry with the matching name was
    /// found, the entry will be returned to the caller.
    ///
    /// Possible errors are:
    ///   * `ZX_ERR_INVALID_ARGS` if `name` exceeds [`fidl_fuchsia_io::MAX_FILENAME`] bytes in
    ///     length.
    fn remove_entry_impl(
        &self,
        name: String,
        must_be_directory: bool,
    ) -> Result<Option<Arc<dyn DirectoryEntry>>, Status>;

    /// Renaming needs to be atomic, even accross two distinct directories.  So we need a special
    /// API to handle that.
    ///
    /// As two distinc directories may mean two mutexes to lock, using it correctly is non-trivial.
    /// In order to avoid a deadlock, we need to decide on a global ordering for the locks.
    /// `rename_from` and [`Self::rename_to`] are used depending on the relative order of the two
    /// directories involved in the operation.
    ///
    /// This method is `unsafe`, as `rename_from` and [`Self::rename_to`] should only be called by
    /// the [`crate::filesystem::FilesystemRename::rename()`], which will establish the global order
    /// and will call proper method.  It should reduce the chances one will use this API
    /// incorrectly.
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
    /// See [`Self::rename_from`] comment for an explanation.
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
    /// This is a companion method to the [`Self::rename_from`]/[`Self::rename_to`] pair.
    /// [`crate::filesystem::FilesystemRename::rename()`] will use this method to avoid locking the
    /// same directory mutex twice.
    ///
    /// It should only be used by the [`crate::filesystem::FilesystemRename::rename()`].
    fn rename_within(&self, src: String, dst: String) -> Result<(), Status>;

    /// Get the filesystem this directory belongs to.
    fn get_filesystem(&self) -> &dyn Filesystem;
}

#[async_trait]
impl<T: DirectlyMutable> MutableDirectory for T {
    async fn unlink(&self, name: &str, must_be_directory: bool) -> Result<(), Status> {
        match self.remove_entry_impl(name.into(), must_be_directory) {
            Ok(Some(_)) => Ok(()),
            Ok(None) => Err(Status::NOT_FOUND),
            Err(e) => Err(e),
        }
    }

    async fn set_attrs(&self, _flags: u32, _attrs: NodeAttributes) -> Result<(), Status> {
        Err(Status::NOT_SUPPORTED)
    }

    fn get_filesystem(&self) -> &dyn Filesystem {
        (self as &dyn DirectlyMutable).get_filesystem()
    }

    async fn sync(&self) -> Result<(), Status> {
        Err(Status::NOT_SUPPORTED)
    }
}
