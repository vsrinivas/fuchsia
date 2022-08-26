// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        directory::FatDirectory,
        refs::FatfsDirRef,
        types::{Dir, Disk, FileSystem},
        util::fatfs_error_to_status,
        FATFS_INFO_NAME, MAX_FILENAME_LEN,
    },
    anyhow::Error,
    fatfs::{self, DefaultTimeProvider, FsOptions, LossyOemCpConverter},
    fidl_fuchsia_io as fio,
    fuchsia_async::{Task, Time, Timer},
    fuchsia_zircon::{AsHandleRef, Event},
    fuchsia_zircon::{Duration, Status},
    std::{
        marker::PhantomPinned,
        pin::Pin,
        sync::{Arc, LockResult, Mutex, MutexGuard},
    },
};

pub struct FatFilesystemInner {
    filesystem: Option<FileSystem>,
    // We don't implement unpin: we want `filesystem` to be pinned so that we can be sure
    // references to filesystem objects (see refs.rs) will remain valid across different locks.
    _pinned: PhantomPinned,
}

impl FatFilesystemInner {
    /// Get the root fatfs Dir.
    pub fn root_dir(&self) -> Dir<'_> {
        self.filesystem.as_ref().unwrap().root_dir()
    }

    pub fn with_disk<F, T>(&self, func: F) -> T
    where
        F: FnOnce(&Box<dyn Disk>) -> T,
    {
        self.filesystem.as_ref().unwrap().with_disk(func)
    }

    pub fn shut_down(&mut self) -> Result<(), Status> {
        self.filesystem.take().ok_or(Status::BAD_STATE)?.unmount().map_err(fatfs_error_to_status)
    }

    pub fn cluster_size(&self) -> u32 {
        self.filesystem.as_ref().map_or(0, |f| f.cluster_size())
    }

    pub fn total_clusters(&self) -> Result<u32, Status> {
        Ok(self
            .filesystem
            .as_ref()
            .ok_or(Status::BAD_STATE)?
            .stats()
            .map_err(fatfs_error_to_status)?
            .total_clusters())
    }

    pub fn free_clusters(&self) -> Result<u32, Status> {
        Ok(self
            .filesystem
            .as_ref()
            .ok_or(Status::BAD_STATE)?
            .stats()
            .map_err(fatfs_error_to_status)?
            .free_clusters())
    }

    pub fn sector_size(&self) -> Result<u16, Status> {
        Ok(self
            .filesystem
            .as_ref()
            .ok_or(Status::BAD_STATE)?
            .stats()
            .map_err(fatfs_error_to_status)?
            .sector_size())
    }
}

pub struct FatFilesystem {
    inner: Mutex<FatFilesystemInner>,
    dirty_task: Mutex<Option<(Time, Task<()>)>>,
    fs_id: Event,
}

impl FatFilesystem {
    /// Create a new FatFilesystem.
    pub fn new(
        disk: Box<dyn Disk>,
        options: FsOptions<DefaultTimeProvider, LossyOemCpConverter>,
    ) -> Result<(Pin<Arc<Self>>, Arc<FatDirectory>), Error> {
        let inner = Mutex::new(FatFilesystemInner {
            filesystem: Some(fatfs::FileSystem::new(disk, options)?),
            _pinned: PhantomPinned,
        });
        let result = Arc::pin(FatFilesystem {
            inner,
            dirty_task: Mutex::new(None),
            fs_id: Event::create()?,
        });
        Ok((result.clone(), result.root_dir()))
    }

    #[cfg(test)]
    pub fn from_filesystem(filesystem: FileSystem) -> (Pin<Arc<Self>>, Arc<FatDirectory>) {
        let inner =
            Mutex::new(FatFilesystemInner { filesystem: Some(filesystem), _pinned: PhantomPinned });
        let result = Arc::pin(FatFilesystem {
            inner,
            dirty_task: Mutex::new(None),
            fs_id: Event::create().unwrap(),
        });
        (result.clone(), result.root_dir())
    }

    pub fn fs_id(&self) -> &Event {
        &self.fs_id
    }

    /// Get the FatDirectory that represents the root directory of this filesystem.
    /// Note this should only be called once per filesystem, otherwise multiple conflicting
    /// FatDirectories will exist.
    /// We only call it from new() and from_filesystem().
    fn root_dir(self: Pin<Arc<Self>>) -> Arc<FatDirectory> {
        // We start with an empty FatfsDirRef and an open_count of zero.
        let dir = FatfsDirRef::empty();
        FatDirectory::new(dir, None, self, "/".to_owned())
    }

    /// Try and lock the underlying filesystem. Returns a LockResult, see `Mutex::lock`.
    pub fn lock(&self) -> LockResult<MutexGuard<'_, FatFilesystemInner>> {
        self.inner.lock()
    }

    /// Mark the filesystem as dirty. This will cause the disk to automatically be flushed after
    /// one second, and cancel any previous pending flushes.
    pub fn mark_dirty(self: &Pin<Arc<Self>>) {
        let deadline = Time::after(Duration::from_seconds(1));
        match &mut *self.dirty_task.lock().unwrap() {
            Some((time, _)) => *time = deadline,
            x @ None => {
                let this = self.clone();
                *x = Some((
                    deadline,
                    Task::spawn(async move {
                        loop {
                            let deadline;
                            {
                                let mut task = this.dirty_task.lock().unwrap();
                                deadline = task.as_ref().unwrap().0;
                                if Time::now() >= deadline {
                                    *task = None;
                                    break;
                                }
                            }
                            Timer::new(deadline).await;
                        }
                        let _ = this.lock().unwrap().filesystem.as_ref().map(|f| f.flush());
                    }),
                ));
            }
        }
    }

    pub fn query_filesystem(&self) -> Result<fio::FilesystemInfo, Status> {
        let fs_lock = self.lock().unwrap();

        let cluster_size = fs_lock.cluster_size() as u64;
        let total_clusters = fs_lock.total_clusters()? as u64;
        let free_clusters = fs_lock.free_clusters()? as u64;
        let total_bytes = cluster_size * total_clusters;
        let used_bytes = cluster_size * (total_clusters - free_clusters);

        Ok(fio::FilesystemInfo {
            total_bytes,
            used_bytes,
            total_nodes: 0,
            used_nodes: 0,
            free_shared_pool_bytes: 0,
            fs_id: self.fs_id().get_koid()?.raw_koid(),
            block_size: cluster_size as u32,
            max_filename_size: MAX_FILENAME_LEN,
            fs_type: fidl_fuchsia_fs::VfsType::Fatfs.into_primitive(),
            padding: 0,
            name: FATFS_INFO_NAME,
        })
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            node::Node,
            tests::{TestDiskContents, TestFatDisk},
        },
        fidl::endpoints::Proxy,
        scopeguard::defer,
        vfs::{directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path::Path},
    };

    const TEST_DISK_SIZE: u64 = 2048 << 10; // 2048K

    #[fuchsia_async::run_singlethreaded(test)]
    #[ignore] // TODO(fxbug.dev/56138): Clean up tasks to prevent panic on drop in FatfsFileRef
    async fn test_automatic_flush() {
        let disk = TestFatDisk::empty_disk(TEST_DISK_SIZE);
        let structure = TestDiskContents::dir().add_child("test", "Hello".into());
        structure.create(&disk.root_dir());

        let fs = disk.into_fatfs();
        let dir = fs.get_fatfs_root();
        dir.open_ref(&fs.filesystem().lock().unwrap()).unwrap();
        defer! { dir.close_ref(&fs.filesystem().lock().unwrap()) };

        let scope = ExecutionScope::new();
        let (proxy, server_end) = fidl::endpoints::create_proxy::<fio::NodeMarker>().unwrap();
        dir.clone().open(
            scope.clone(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            0,
            Path::validate_and_split("test").unwrap(),
            server_end,
        );

        assert!(fs.filesystem().dirty_task.lock().unwrap().is_none());
        let file = fio::FileProxy::new(proxy.into_channel().unwrap());
        file.write("hello there".as_bytes()).await.unwrap().map_err(Status::from_raw).unwrap();
        {
            let fs_lock = fs.filesystem().lock().unwrap();
            // fs should be dirty until the timer expires.
            assert!(fs_lock.filesystem.as_ref().unwrap().is_dirty());
        }
        // Wait some time for the flush to happen. Don't hold the lock while waiting, otherwise
        // the flush will get stuck waiting on the lock.
        Timer::new(Time::after(Duration::from_millis(1500))).await;
        {
            let fs_lock = fs.filesystem().lock().unwrap();
            assert_eq!(fs_lock.filesystem.as_ref().unwrap().is_dirty(), false);
        }
    }
}
