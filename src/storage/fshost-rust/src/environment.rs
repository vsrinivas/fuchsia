// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::device::Device,
    anyhow::Error,
    async_trait::async_trait,
    fidl::endpoints::{create_proxy, Proxy, ServerEnd},
    fidl_fuchsia_device::ControllerProxy,
    fidl_fuchsia_io as fio,
    fs_management::{filesystem::ServingFilesystem, Blobfs},
    fuchsia_zircon as zx,
};

/// Environment is a trait that performs actions when a device is matched.
#[async_trait]
pub trait Environment: Send + Sync {
    /// Attaches the specified driver to the device.
    async fn attach_driver(
        &mut self,
        device: &mut dyn Device,
        driver_path: &str,
    ) -> Result<(), Error>;

    /// Mounts Blobfs on the given device.
    async fn mount_blobfs(&mut self, device: &mut dyn Device) -> Result<(), Error>;

    /// Mounts the data partition on the given device.
    async fn mount_data(&mut self, device: &mut dyn Device) -> Result<(), Error>;
}

// Before a filesystem is mounted, we queue requests.
enum Filesystem {
    Queue(Vec<ServerEnd<fio::DirectoryMarker>>),
    Serving(ServingFilesystem),
}

/// Implements the Environment trait and keeps track of mounted filesystems.
pub struct FshostEnvironment {
    blobfs: Filesystem,
}

impl FshostEnvironment {
    pub fn new() -> Self {
        Self { blobfs: Filesystem::Queue(Vec::new()) }
    }

    /// Returns a proxy for the root of the Blobfs filesystem.  This can be called before Blobfs is
    /// mounted and it will get routed once Blobfs is mounted.
    pub fn blobfs_root(&mut self) -> Result<fio::DirectoryProxy, Error> {
        let (proxy, server) = create_proxy::<fio::DirectoryMarker>()?;
        match &mut self.blobfs {
            Filesystem::Queue(queue) => queue.push(server),
            Filesystem::Serving(fs) => {
                fs.root().clone(fio::OpenFlags::CLONE_SAME_RIGHTS, server.into_channel().into())?
            }
        }
        Ok(proxy)
    }
}

#[async_trait]
impl Environment for FshostEnvironment {
    async fn attach_driver(
        &mut self,
        device: &mut dyn Device,
        driver_path: &str,
    ) -> Result<(), Error> {
        let controller = ControllerProxy::new(device.proxy()?.into_channel().unwrap());
        controller.bind(driver_path).await?.map_err(zx::Status::from_raw)?;
        Ok(())
    }

    async fn mount_blobfs(&mut self, device: &mut dyn Device) -> Result<(), Error> {
        let fs =
            Blobfs::from_channel(device.proxy()?.into_channel().unwrap().into())?.serve().await?;
        if let Filesystem::Queue(queue) = &mut self.blobfs {
            let root_dir = fs.root();
            for server in queue.drain(..) {
                root_dir.clone(fio::OpenFlags::CLONE_SAME_RIGHTS, server.into_channel().into())?;
            }
        } else {
            panic!("blobfs already mounted");
        }
        self.blobfs = Filesystem::Serving(fs);
        Ok(())
    }

    async fn mount_data(&mut self, _device: &mut dyn Device) -> Result<(), Error> {
        todo!()
    }
}
