// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fdr::execute_reset;
use anyhow::{Context, Error};
use async_trait::async_trait;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_io as fio;
use fs_management as fs;
use recovery_util::block::get_block_devices;

pub const BLOBFS_MOUNT_POINT: &str = "/b";

/// Required functionality from an fs::Filesystem.
/// See fs_management for the documentation.
trait Filesystem {
    fn format(&mut self) -> Result<(), Error>;
    fn mount(&mut self, mount_point: &str) -> Result<(), Error>;
}

/// Forwards calls to the fs_management implementation.
impl Filesystem for fs::Filesystem<fs::Blobfs> {
    fn format(&mut self) -> Result<(), Error> {
        self.format()
    }

    fn mount(&mut self, mount_point: &str) -> Result<(), Error> {
        self.mount(mount_point)
    }
}

#[async_trait(?Send)]
pub trait Initializer {
    async fn initialize(&self) -> Result<Storage, Error>;
}

/// Creates blobfs and minfs fs::Filesystem objects by scanning topological paths.
pub struct TopoPathInitializer {}

#[async_trait(?Send)]
impl Initializer for TopoPathInitializer {
    async fn initialize(&self) -> Result<Storage, Error> {
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

        let blobfs = fs::Filesystem::from_path(&blobfs_path, blobfs_config)
            .context(format!("Failed to open blobfs: {:?}", blobfs_path))?;

        Ok(Storage { blobfs: Box::new(blobfs) })
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
        self.blobfs.format().context("Failed to format blobfs")?;
        self.blobfs.mount(BLOBFS_MOUNT_POINT).context("Failed to mount blobfs")?;
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
    use super::{Filesystem, Initializer, Storage, BLOBFS_MOUNT_POINT};
    use anyhow::Error;
    use async_trait::async_trait;
    use fuchsia_async as fasync;
    use std::{cell::RefCell, rc::Rc};

    // Mock for a Filesystem.
    struct FilesystemMock {
        format_called: Rc<RefCell<bool>>,
        mount_called: Rc<RefCell<bool>>,
    }

    impl FilesystemMock {
        fn new(
            format_call_tracker: Rc<RefCell<bool>>,
            mount_call_tracker: Rc<RefCell<bool>>,
        ) -> FilesystemMock {
            FilesystemMock {
                format_called: format_call_tracker.clone(),
                mount_called: mount_call_tracker.clone(),
            }
        }
    }

    impl Filesystem for FilesystemMock {
        fn format(&mut self) -> Result<(), Error> {
            self.format_called.replace(true);
            Ok(())
        }

        fn mount(&mut self, mount_point: &str) -> Result<(), Error> {
            assert_eq!(mount_point, BLOBFS_MOUNT_POINT);
            self.mount_called.replace(true);
            Ok(())
        }
    }

    struct InitializerMock {
        format_called: Rc<RefCell<bool>>,
        mount_called: Rc<RefCell<bool>>,
    }

    impl InitializerMock {
        async fn new(
            format_call_tracker: Rc<RefCell<bool>>,
            mount_call_tracker: Rc<RefCell<bool>>,
        ) -> Result<InitializerMock, Error> {
            Ok(InitializerMock {
                format_called: format_call_tracker.clone(),
                mount_called: mount_call_tracker.clone(),
            })
        }
    }

    #[async_trait(?Send)]
    impl Initializer for InitializerMock {
        async fn initialize(&self) -> Result<Storage, Error> {
            Ok(Storage {
                blobfs: Box::new(FilesystemMock::new(
                    self.format_called.clone(),
                    self.mount_called.clone(),
                )),
            })
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_wipe_storage() -> Result<(), Error> {
        let format_call_tracker = Rc::new(RefCell::new(false));
        let mount_call_tracker = Rc::new(RefCell::new(false));
        let initializer =
            InitializerMock::new(format_call_tracker.clone(), mount_call_tracker.clone()).await?;
        let mut storage = initializer.initialize().await?;

        storage.wipe_storage().await?;

        // Check that it formatted and mounted blobfs.
        assert!(*format_call_tracker.borrow());
        assert!(*mount_call_tracker.borrow());
        Ok(())
    }
}
