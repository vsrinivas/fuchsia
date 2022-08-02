// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fdr::execute_reset;
use anyhow::{ensure, Context, Error};
use async_trait::async_trait;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_io as fio;
use fs_management::{self as fs, filesystem::ServingFilesystem};
use recovery_util::block::get_block_devices;

pub const BLOBFS_MOUNT_POINT: &str = "/b";

/// Required functionality from an fs::Filesystem.
/// See fs_management for the documentation.
#[async_trait]
trait Filesystem {
    async fn format(&mut self) -> Result<(), Error>;
    async fn mount(&mut self, mount_point: &str) -> Result<(), Error>;
}

/// Forwards calls to the fs_management implementation.
struct Blobfs {
    fs: fs::filesystem::Filesystem<fs::Blobfs>,
    serving_filesystem: Option<ServingFilesystem>,
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
}

/// Creates blobfs and minfs Filesystem objects by scanning topological paths.
pub struct TopoPathInitializer {}

impl TopoPathInitializer {
    pub async fn initialize(&self) -> Result<Storage, Error> {
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

        Ok(Storage {
            blobfs: Box::new(Blobfs {
                fs: fs::filesystem::Filesystem::from_path(&blobfs_path, blobfs_config)
                    .context(format!("Failed to open blobfs: {:?}", blobfs_path))?,
                serving_filesystem: None,
            }),
        })
    }
}

/// The object that controls the lifetime of the newly formatted blobfs.
/// The filesystem is accessible through the "/b" path on the current namespace,
/// as long as this object is alive.
pub struct Storage {
    blobfs: Box<dyn Filesystem>,
}

impl Storage {
    // TODO(fxbug.dev/100049): Switch to storage API which can handle reprovisioning FVM and minfs.
    /// Reformat filesystems, then mount available filesystems.
    pub async fn wipe_storage(&mut self) -> Result<(), Error> {
        self.blobfs.format().await.context("Failed to format blobfs")?;
        self.blobfs.mount(BLOBFS_MOUNT_POINT).await.context("Failed to mount blobfs")?;
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

    pub fn get_blobfs(&mut self) -> Result<ClientEnd<fio::DirectoryMarker>, Error> {
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

#[cfg(test)]
mod tests {
    use super::{Filesystem, Storage, BLOBFS_MOUNT_POINT};
    use anyhow::Error;
    use async_trait::async_trait;
    use fuchsia_async as fasync;
    use std::sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    };

    // Mock for a Filesystem.
    #[derive(Default)]
    struct Inner {
        format_called: AtomicBool,
        mount_called: AtomicBool,
    }

    #[derive(Clone)]
    struct FilesystemMock(Arc<Inner>);

    impl FilesystemMock {
        fn new() -> Self {
            FilesystemMock(Arc::new(Inner::default()))
        }
    }

    #[async_trait]
    impl Filesystem for FilesystemMock {
        async fn format(&mut self) -> Result<(), Error> {
            self.0.format_called.store(true, Ordering::SeqCst);
            Ok(())
        }

        async fn mount(&mut self, mount_point: &str) -> Result<(), Error> {
            assert_eq!(mount_point, BLOBFS_MOUNT_POINT);
            self.0.mount_called.store(true, Ordering::SeqCst);
            Ok(())
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_wipe_storage() -> Result<(), Error> {
        let fs_mock = FilesystemMock::new();
        let mut storage = Storage { blobfs: Box::new(fs_mock.clone()) };

        storage.wipe_storage().await?;

        // Check that it formatted and mounted blobfs.
        assert!(fs_mock.0.format_called.load(Ordering::SeqCst));
        assert!(fs_mock.0.mount_called.load(Ordering::SeqCst));
        Ok(())
    }
}
