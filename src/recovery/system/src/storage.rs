// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{ensure, Context, Error};
use async_trait::async_trait;
use fdr_lib::execute_reset;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_io as fio;
use fs_management::{self as fs, filesystem::ServingSingleVolumeFilesystem};
use futures::lock::Mutex;
use recovery_util::block::get_block_devices;
use std::sync::Arc;

pub const BLOBFS_MOUNT_POINT: &str = "/b";

/// Required functionality from an fs::Filesystem.
/// See fs_management for the documentation.
#[async_trait]
trait Filesystem {
    async fn format(&mut self) -> Result<(), Error>;
    async fn mount(&mut self, mount_point: &str) -> Result<(), Error>;
    fn is_mounted(&self) -> bool;
}

/// Forwards calls to the fs_management implementation.
struct Blobfs {
    fs: fs::filesystem::Filesystem<fs::Blobfs>,
    serving_filesystem: Option<ServingSingleVolumeFilesystem>,
}

#[async_trait]
impl Filesystem for Blobfs {
    async fn format(&mut self) -> Result<(), Error> {
        self.fs.format().await
    }

    async fn mount(&mut self, mount_point: &str) -> Result<(), Error> {
        ensure!(self.serving_filesystem.is_none(), "Already mounted");
        let mut fs = self.fs.serve().await?;
        fs.bind_to_path(mount_point)?;
        self.serving_filesystem = Some(fs);
        Ok(())
    }

    fn is_mounted(&self) -> bool {
        self.serving_filesystem.is_some()
    }
}

#[async_trait]
pub trait StorageFactory<T: Storage> {
    async fn create(self) -> Result<T, Error>;
}

/// Creates blobfs and minfs Filesystem objects by scanning topological paths.
pub struct TopoPathInitializer {}

#[async_trait]
impl StorageFactory<RealStorage> for TopoPathInitializer {
    async fn create(self) -> Result<RealStorage, Error> {
        // TODO(b/235401377): Add tests when component is moved to CFv2.
        let block_devices = get_block_devices().await.context("Failed to get block devices")?;

        let blobfs_config = fs::Blobfs { readonly: false, ..fs::Blobfs::default() };

        let mut blobfs_path = "".to_string();
        for block_device in block_devices {
            let topo_path = block_device.topo_path;
            // Skip ramdisk entries.
            if topo_path.contains("/ramdisk-") {
                continue;
            }

            // Store the path as /dev/class/block/<number>.
            if topo_path.contains("/fvm/blobfs-") {
                blobfs_path = block_device.class_path.clone();
            }
        }

        Ok(RealStorage {
            blobfs: Arc::new(Mutex::new(Blobfs {
                fs: fs::filesystem::Filesystem::from_path(&blobfs_path, blobfs_config)
                    .context(format!("Failed to open blobfs: {:?}", blobfs_path))?,
                serving_filesystem: None,
            })),
        })
    }
}

#[async_trait]
pub trait Storage {
    /// Wipes storage on the first call then returns the formatted directory.
    async fn wipe_or_get_storage(&self) -> Result<ClientEnd<fio::DirectoryMarker>, Error>;
    /// Wipes stored data.
    async fn wipe_data(&self) -> Result<(), Error>;
}

/// The object that controls the lifetime of the newly formatted blobfs.
/// The filesystem is accessible through the "/b" path on the current namespace,
/// as long as this object is alive.
pub struct RealStorage {
    blobfs: Arc<Mutex<dyn Filesystem + Send>>,
}

impl RealStorage {
    // TODO(fxbug.dev/100049): Switch to storage API which can handle reprovisioning FVM and minfs.
    /// Reformat filesystems, then mount available filesystems.
    pub async fn wipe_storage(&self) -> Result<(), Error> {
        self.blobfs.lock().await.format().await.context("Failed to format blobfs")?;
        self.blobfs
            .lock()
            .await
            .mount(BLOBFS_MOUNT_POINT)
            .await
            .context("Failed to mount blobfs")?;
        Ok(())
    }

    // Instead of formatting the data partition directly, reset it via the factory reset service.
    // The data partition is reformatted on first boot under normal circumstances and will do so
    // after a reboot following being reset.
    // This immediately reboots the device and needs to run separately from wipe_storage for now.
    pub async fn wipe_data(&self) -> Result<(), Error> {
        execute_reset().await.context("Failed to factory reset data")
    }

    /// Get the minfs mount point if it has been properly wiped and reinitialized.
    pub fn get_minfs_mount_point(&self) -> Result<Option<String>, Error> {
        println!("Formatting new minfs is not currently supported in recovery.");
        Ok(None)
    }

    pub fn get_blobfs(&self) -> Result<ClientEnd<fio::DirectoryMarker>, Error> {
        let (blobfs_root, remote) = fidl::endpoints::create_endpoints::<fio::DirectoryMarker>()?;
        fdio::open(
            BLOBFS_MOUNT_POINT,
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::RIGHT_EXECUTABLE,
            remote.into_channel(),
        )?;
        Ok(blobfs_root)
    }
}

#[async_trait]
impl Storage for RealStorage {
    async fn wipe_or_get_storage(&self) -> Result<ClientEnd<fio::DirectoryMarker>, Error> {
        // If we're not serving a file system then one needs to be mounted.
        // Otherwise, we already have a directory to use.
        if !self.blobfs.lock().await.is_mounted() {
            self.wipe_storage().await?;
        }
        self.get_blobfs()
    }

    async fn wipe_data(&self) -> Result<(), Error> {
        self.wipe_data().await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::Error;
    use async_trait::async_trait;
    use fidl::endpoints::ServerEnd;
    use fuchsia_async as fasync;
    use futures::lock::Mutex;
    use std::sync::{
        atomic::{AtomicU8, Ordering},
        Arc,
    };
    use vfs::{directory::entry::DirectoryEntry, file::vmo::read_only_static};

    // Mock for a Filesystem.
    #[derive(Default)]
    struct Inner {
        format_calls: AtomicU8,
        mount_calls: AtomicU8,
    }

    #[derive(Clone)]
    struct FilesystemMock {
        inner: Arc<Inner>,
        dir: Arc<dyn DirectoryEntry>,
    }

    impl FilesystemMock {
        fn new() -> Self {
            FilesystemMock {
                inner: Arc::new(Inner::default()),
                dir: vfs::pseudo_directory! {
                    "testfile" => read_only_static("test123"),
                },
            }
        }
    }

    #[async_trait]
    impl Filesystem for FilesystemMock {
        async fn format(&mut self) -> Result<(), Error> {
            self.inner.format_calls.fetch_add(1, Ordering::SeqCst);
            Ok(())
        }

        async fn mount(&mut self, mount_point: &str) -> Result<(), Error> {
            assert_eq!(mount_point, BLOBFS_MOUNT_POINT);
            self.inner.mount_calls.fetch_add(1, Ordering::SeqCst);

            let namespace = fdio::Namespace::installed().unwrap();
            let (client, server) =
                fidl::endpoints::create_endpoints::<fio::DirectoryMarker>().unwrap();

            let scope = vfs::execution_scope::ExecutionScope::new();
            self.dir.clone().open(
                scope.clone(),
                fio::OpenFlags::RIGHT_READABLE
                    | fio::OpenFlags::RIGHT_WRITABLE
                    | fio::OpenFlags::RIGHT_EXECUTABLE,
                fio::MODE_TYPE_DIRECTORY,
                vfs::path::Path::dot(),
                ServerEnd::new(server.into_channel()),
            );

            fasync::Task::local(async move {
                scope.wait().await;
            })
            .detach();

            namespace.bind(BLOBFS_MOUNT_POINT, client).unwrap();
            Ok(())
        }

        fn is_mounted(&self) -> bool {
            self.inner.mount_calls.load(Ordering::SeqCst) > 0
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_wipe_storage() -> Result<(), Error> {
        let fs_mock = FilesystemMock::new();
        let storage = RealStorage { blobfs: Arc::new(Mutex::new(fs_mock.clone())) };

        storage.wipe_storage().await?;

        // Check that it formatted and mounted blobfs.
        assert_eq!(1, fs_mock.inner.format_calls.load(Ordering::SeqCst));
        assert_eq!(1, fs_mock.inner.mount_calls.load(Ordering::SeqCst));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_real_storage_blobfs() -> Result<(), Error> {
        let fs_mock = FilesystemMock::new();
        let storage = RealStorage { blobfs: Arc::new(Mutex::new(fs_mock.clone())) };

        {
            let dir = storage.wipe_or_get_storage().await.unwrap().into_proxy().unwrap();
            let flags = fio::OpenFlags::RIGHT_READABLE;
            let file = fuchsia_fs::directory::open_file(&dir, "testfile", flags).await.unwrap();
            let contents = fuchsia_fs::file::read_to_string(&file).await.unwrap();
            assert_eq!("test123".to_string(), contents);
        }

        // Second try should work the same.
        {
            let dir = storage.wipe_or_get_storage().await.unwrap().into_proxy().unwrap();
            let flags = fio::OpenFlags::RIGHT_READABLE;
            let file = fuchsia_fs::directory::open_file(&dir, "testfile", flags).await.unwrap();
            let contents = fuchsia_fs::file::read_to_string(&file).await.unwrap();
            assert_eq!("test123".to_string(), contents);
        }

        // Check that it formatted and mounted blobfs only once.
        assert_eq!(1, fs_mock.inner.format_calls.load(Ordering::SeqCst));
        assert_eq!(1, fs_mock.inner.mount_calls.load(Ordering::SeqCst));
        Ok(())
    }
}
