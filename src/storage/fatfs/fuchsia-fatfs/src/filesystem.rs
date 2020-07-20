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
    vfs::filesystem::{Filesystem, FilesystemRename},
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
        src_name: String,
        dst_dir: Arc<dyn Any + Sync + Send + 'static>,
        dst_name: String,
    ) -> Result<(), Status> {
        let src_dir = src_dir.downcast::<FatDirectory>().map_err(|_| Status::INVALID_ARGS)?;
        let dst_dir = dst_dir.downcast::<FatDirectory>().map_err(|_| Status::INVALID_ARGS)?;

        // TODO(simonshields): We currently require there is only one reference to the child.
        // We should fix this by just moving the Arc<> and updating the parent of the child instead of doing this.
        match src_dir.cache_get(&src_name) {
            // References to the child still exist, not safe to rename.
            Some(_) => return Err(Status::UNAVAILABLE),
            // Not opened by anyone.
            // TODO(fxb/56009): this is racy - we do not hold the filesystem lock here, so it's
            // possible that another thread could add the entry to the cache before we acquire the
            // filesystem lock. More investigation will be required to make sure acquiring the
            // filesystem lock earlier is safe.
            None => {}
        };

        let filesystem = self.inner.lock().unwrap();
        let src_dir = src_dir.borrow_dir(&filesystem);
        let dst_dir = dst_dir.borrow_dir(&filesystem);
        src_dir.rename(&src_name, &dst_dir, &dst_name).map_err(fatfs_error_to_status)?;
        Ok(())
    }
}

impl Filesystem for FatFilesystem {}
