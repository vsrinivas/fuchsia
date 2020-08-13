// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        directory::{FatDirectory, InsensitiveStringRef},
        node::{Closer, FatNode, Node},
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
        // We start with an empty FatfsDirRef and an open_count of zero.
        let dir = FatfsDirRef::empty();
        FatDirectory::new(dir, None, self, "/".to_owned())
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

    /// Do a simple rename of the file, without unlinking dst.
    /// This assumes that either "dst" and "src" are the same file, or that "dst" has already been
    /// unlinked.
    fn rename_internal(
        &self,
        filesystem: &FatFilesystemInner,
        src_dir: &Arc<FatDirectory>,
        src_name: &str,
        dst_dir: &Arc<FatDirectory>,
        dst_name: &str,
        existing: ExistingRef<'_, '_>,
    ) -> Result<(), Status> {
        // We're ready to go: remove the entry from the source cache, and close the reference to
        // the underlying file (this ensures all pending writes, etc. have been flushed).
        // We remove the entry with rename() below, and hold the filesystem lock so nothing will
        // put the entry back in the cache. After renaming we also re-attach the entry to its
        // parent.

        // Do the rename.
        let src_fatfs_dir = src_dir.borrow_dir(&filesystem)?;
        let dst_fatfs_dir = dst_dir.borrow_dir(&filesystem)?;

        match existing {
            ExistingRef::None => {
                src_fatfs_dir
                    .rename(src_name, &dst_fatfs_dir, dst_name)
                    .map_err(fatfs_error_to_status)?;
            }
            ExistingRef::File(file) => {
                src_fatfs_dir
                    .rename_over_file(src_name, &dst_fatfs_dir, dst_name, file)
                    .map_err(fatfs_error_to_status)?;
            }
            ExistingRef::Dir(dir) => {
                src_fatfs_dir
                    .rename_over_dir(src_name, &dst_fatfs_dir, dst_name, dir)
                    .map_err(fatfs_error_to_status)?;
            }
        }

        src_dir.did_remove(src_name);
        dst_dir.did_add(dst_name);

        // TODO: do the watcher event for existing.

        Ok(())
    }

    /// Helper for rename which returns FatNodes that need to be dropped without the fs lock held.
    fn rename_locked(
        &self,
        filesystem: &FatFilesystemInner,
        src_dir: &Arc<FatDirectory>,
        src_name: &str,
        dst_dir: &Arc<FatDirectory>,
        dst_name: &str,
        src_is_dir: bool,
        closer: &mut Closer<'_>,
    ) -> Result<(), Status> {
        // Renaming a file to itself is trivial, but we do it after we've checked that the file
        // exists and that src and dst have the same type.
        if Arc::ptr_eq(&src_dir, &dst_dir)
            && (&src_name as &dyn InsensitiveStringRef) == (&dst_name as &dyn InsensitiveStringRef)
        {
            if src_name != dst_name {
                // Cases don't match - we don't unlink, but we still need to fix the file's LFN.
                return self.rename_internal(
                    &filesystem,
                    src_dir,
                    src_name,
                    dst_dir,
                    dst_name,
                    ExistingRef::None,
                );
            }
            return Ok(());
        }

        if let Some(src_node) = src_dir.cache_get(src_name) {
            // We can't move a directory into itself.
            if let FatNode::Dir(ref dir) = src_node {
                if Arc::ptr_eq(&dir, &dst_dir) {
                    return Err(Status::INVALID_ARGS);
                }
            }
            src_node.flush_dir_entry(filesystem)?;
        }

        let mut dir;
        let mut file;
        let mut existing_node = dst_dir.cache_get(dst_name);
        let existing = match existing_node {
            None => {
                dst_dir.open_ref(filesystem)?;
                closer.add(FatNode::Dir(dst_dir.clone()));
                match dst_dir.find_child(filesystem, dst_name)? {
                    Some(ref dir_entry) => {
                        if dir_entry.is_dir() {
                            dir = Some(dir_entry.to_dir());
                            ExistingRef::Dir(dir.as_mut().unwrap())
                        } else {
                            file = Some(dir_entry.to_file());
                            ExistingRef::File(file.as_mut().unwrap())
                        }
                    }
                    None => ExistingRef::None,
                }
            }
            Some(ref mut node) => {
                node.open_ref(filesystem)?;
                closer.add(node.clone());
                match node {
                    FatNode::Dir(ref mut node_dir) => {
                        ExistingRef::Dir(node_dir.borrow_dir_mut(filesystem).unwrap())
                    }
                    FatNode::File(ref mut node_file) => {
                        ExistingRef::File(node_file.borrow_file_mut(filesystem).unwrap())
                    }
                }
            }
        };

        match existing {
            ExistingRef::File(_) => {
                if src_is_dir {
                    return Err(Status::NOT_DIR);
                }
            }
            ExistingRef::Dir(_) => {
                if !src_is_dir {
                    return Err(Status::NOT_FILE);
                }
            }
            ExistingRef::None => {}
        }

        self.rename_internal(&filesystem, src_dir, src_name, dst_dir, dst_name, existing)?;

        if let Some(_) = existing_node {
            dst_dir.cache_remove(&filesystem, &dst_name).unwrap().did_delete();
        }

        // We suceeded in renaming, so now move the nodes around.
        if let Some(node) = src_dir.remove_child(&filesystem, &src_name) {
            dst_dir
                .add_child(&filesystem, dst_name.to_owned(), node)
                .unwrap_or_else(|e| panic!("Rename failed, but fatfs says it didn't? - {:?}", e));
        }

        Ok(())
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

        let mut closer = Closer::new(&self);
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
            // The caller wanted a directory (src or dst), but src is not a directory. This is
            // an error.
            return Err(Status::NOT_DIR);
        }

        self.rename_locked(
            &filesystem,
            &src_dir,
            src_name,
            &dst_dir,
            dst_name,
            src_is_dir,
            &mut closer,
        )
    }
}

impl Filesystem for FatFilesystem {}

pub(crate) enum ExistingRef<'a, 'b> {
    None,
    File(&'a mut crate::types::File<'b>),
    Dir(&'a mut crate::types::Dir<'b>),
}
