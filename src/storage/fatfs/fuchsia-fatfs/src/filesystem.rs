// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        directory::FatDirectory,
        refs::FatfsDirRef,
        types::{Dir, FileSystem},
        util::fatfs_error_to_status,
    },
    anyhow::Error,
    fatfs::{self, DefaultTimeProvider, FsOptions, LossyOemCpConverter, ReadWriteSeek},
    fuchsia_zircon::Status,
    std::{
        any::Any,
        marker::PhantomPinned,
        pin::Pin,
        sync::{Arc, LockResult, Mutex, MutexGuard},
    },
    vfs::{
        filesystem::{Filesystem, FilesystemRename},
        path::Path,
    },
};

pub struct FatFilesystemInner {
    filesystem: FileSystem,
    // We don't implement unpin: we want `filesystem` to be pinned so that we can be sure
    // references to filesystem objects (see refs.rs) will remain valid across different locks.
    _pinned: PhantomPinned,
}

impl FatFilesystemInner {
    /// Get the root fatfs Dir.
    pub fn root_dir(&self) -> Dir<'_> {
        self.filesystem.root_dir()
    }

    pub fn shut_down(self) -> Result<(), Status> {
        self.filesystem.unmount().map_err(fatfs_error_to_status)
        // TODO(55291): send flush to the underlying block device.
    }

    pub fn cluster_size(&self) -> u32 {
        self.filesystem.cluster_size()
    }
}

pub struct FatFilesystem {
    inner: Pin<Arc<Mutex<FatFilesystemInner>>>,
}

impl FatFilesystem {
    /// Create a new FatFilesystem.
    pub fn new(
        disk: Box<dyn ReadWriteSeek + Send>,
        options: FsOptions<DefaultTimeProvider, LossyOemCpConverter>,
    ) -> Result<(Arc<Self>, Arc<FatDirectory>), Error> {
        let inner = Arc::pin(Mutex::new(FatFilesystemInner {
            filesystem: fatfs::FileSystem::new(disk, options)?,
            _pinned: PhantomPinned,
        }));
        let result = Arc::new(FatFilesystem { inner });
        Ok((result.clone(), result.root_dir()))
    }

    #[cfg(test)]
    pub fn from_filesystem(filesystem: FileSystem) -> (Arc<Self>, Arc<FatDirectory>) {
        let inner = Arc::pin(Mutex::new(FatFilesystemInner { filesystem, _pinned: PhantomPinned }));
        let result = Arc::new(FatFilesystem { inner });
        (result.clone(), result.root_dir())
    }

    /// Get the FatDirectory that represents the root directory of this filesystem.
    /// Note this should only be called once per filesystem, otherwise multiple conflicting
    /// FatDirectories will exist.
    /// We only call it from new() and from_filesystem().
    fn root_dir(self: Arc<Self>) -> Arc<FatDirectory> {
        let clone = self.clone();
        let fs_lock = clone.inner.lock().unwrap();
        let dir = unsafe { FatfsDirRef::from(fs_lock.root_dir()) };
        FatDirectory::new(dir, None, self)
    }

    /// Try and lock the underlying filesystem. Returns a LockResult, see `Mutex::lock`.
    pub fn lock(&self) -> LockResult<MutexGuard<'_, FatFilesystemInner>> {
        self.inner.lock()
    }

    /// Cleanly shut down the filesystem.
    pub fn shut_down(self) -> Result<(), Status> {
        // This is safe because we hold the only reference to `inner`, so there are no stray
        // references to fatfs Dir or Files.
        let arc = unsafe { Pin::into_inner_unchecked(self.inner) };
        let mutex = Arc::try_unwrap(arc).map_err(|_| Status::UNAVAILABLE)?;
        let inner = mutex.into_inner().map_err(|_| Status::UNAVAILABLE)?;
        inner.shut_down()
    }
}

impl FilesystemRename for FatFilesystem {
    fn rename(
        &self,
        src_dir: Arc<dyn Any + Sync + Send + 'static>,
        src_path: Path,
        dst_dir: Arc<dyn Any + Sync + Send + 'static>,
        dst_path: Path,
    ) -> Result<(), Status> {
        let src_dir = src_dir.downcast::<FatDirectory>().map_err(|_| Status::INVALID_ARGS)?;
        let dst_dir = dst_dir.downcast::<FatDirectory>().map_err(|_| Status::INVALID_ARGS)?;
        if dst_dir.is_deleted() {
            // Can't rename into a deleted folder.
            return Err(Status::NOT_FOUND);
        }

        let src_name = src_path.peek().unwrap();
        let dst_name = dst_path.peek().unwrap();

        // Renaming a file to itself is trivial.
        if Arc::ptr_eq(&src_dir, &dst_dir) && src_name == dst_name {
            return Ok(());
        }

        let filesystem = self.inner.lock().unwrap();

        // Figure out if src is a directory.
        let entry = src_dir.find_child(&filesystem, &src_name)?;
        if entry.is_none() {
            // No such src (if we don't return NOT_FOUND here, fatfs will return it when we
            // call rename() later).
            return Err(Status::NOT_FOUND);
        }
        let src_is_dir = entry.unwrap().is_dir();
        if (dst_path.is_dir() || src_path.is_dir()) && !src_is_dir {
            // The caller wanted a directory (src or dst), but src is not a directory. This is an error.
            return Err(Status::NOT_DIR);
        }

        // Make sure destination is a directory, if needed.
        if let Some(entry) = dst_dir.find_child(&filesystem, &dst_name)? {
            if entry.is_dir() {
                // Try to rename over directory. The src must be a directory, and dst must be empty.
                let dir = entry.to_dir();
                if !src_is_dir {
                    return Err(Status::NOT_DIR);
                }

                if !dir.is_empty().map_err(fatfs_error_to_status)? {
                    // Can't rename directory onto non-empty directory.
                    return Err(Status::NOT_EMPTY);
                }

                // TODO(fxb/56239) allow this path once we support overwriting.
                return Err(Status::ALREADY_EXISTS);
            } else {
                if src_is_dir {
                    // We were expecting dst to be a directory, but it wasn't.
                    return Err(Status::NOT_DIR);
                }
                // TODO(fxb/56239) allow this path once we support overwriting.
                return Err(Status::ALREADY_EXISTS);
            }
        }

        // We're ready to go: remove the entry from the source cache, and close the reference to
        // the underlying file (this ensures all pending writes, etc. have been flushed).
        // We remove the entry with rename() below, and hold the filesystem lock so nothing will
        // put the entry back in the cache. After renaming we also re-attach the entry to its
        // parent.
        let cache_entry = src_dir.remove_child(&filesystem, &src_name);

        // Do the rename.
        let src_fatfs_dir = src_dir.borrow_dir(&filesystem)?;
        let dst_fatfs_dir = dst_dir.borrow_dir(&filesystem)?;
        let result =
            src_fatfs_dir.rename(src_name, &dst_fatfs_dir, dst_name).map_err(fatfs_error_to_status);
        if let Err(e) = result {
            // TODO(fxb/56239): We are potentially serving connections to a node that is no longer
            // "on" the filesystem at this point.
            // We need to handle this more gracefully. Ideally we'd change fatfs to make rename
            // atomic, so there's no risk of this "in-between" state.
            // Failing that, we should at least make sure that if the src is a directory we
            // recursively delete all of its children.
            // For now, we hope that the entry is still there, and if not just give up on it -
            // the Fat{File,Directory} will return Status::UNAVAILABLE to all requests.
            if let Some(cache_entry) = cache_entry {
                src_dir.add_child(&filesystem, src_name.to_owned(), cache_entry)?;
            }
            return Err(e);
        }

        if let Some(cache_entry) = cache_entry {
            // We just renamed here, and the rename would've failed if there was an
            // existing entry there.
            dst_dir
                .add_child(&filesystem, dst_name.to_owned(), cache_entry)
                .unwrap_or_else(|e| panic!("Rename failed, but fatfs says it didn't? - {:?}", e));
        }
        Ok(())
    }
}

impl Filesystem for FatFilesystem {}
