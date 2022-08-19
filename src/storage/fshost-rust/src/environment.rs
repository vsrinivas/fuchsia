// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{device::Device, fxfs},
    anyhow::Error,
    async_trait::async_trait,
    fidl::endpoints::{create_proxy, Proxy, ServerEnd},
    fidl_fuchsia_device::ControllerProxy,
    fidl_fuchsia_io as fio,
    fs_management::{
        filesystem::{ServingMultiVolumeFilesystem, ServingSingleVolumeFilesystem},
        Blobfs, Fxfs,
    },
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
    Serving(ServingSingleVolumeFilesystem),
    ServingMultiVolume(ServingMultiVolumeFilesystem),
}

impl Filesystem {
    fn root(&mut self) -> Result<fio::DirectoryProxy, Error> {
        let (proxy, server) = create_proxy::<fio::DirectoryMarker>()?;
        match self {
            Filesystem::Queue(queue) => queue.push(server),
            Filesystem::Serving(fs) => {
                fs.root().clone(fio::OpenFlags::CLONE_SAME_RIGHTS, server.into_channel().into())?
            }
            Filesystem::ServingMultiVolume(fs) => fs
                .volume("default")
                .unwrap()
                .root()
                .clone(fio::OpenFlags::CLONE_SAME_RIGHTS, server.into_channel().into())?,
        }
        Ok(proxy)
    }
}

/// Implements the Environment trait and keeps track of mounted filesystems.
pub struct FshostEnvironment {
    blobfs: Filesystem,
    data: Filesystem,
}

impl FshostEnvironment {
    pub fn new() -> Self {
        Self { blobfs: Filesystem::Queue(Vec::new()), data: Filesystem::Queue(Vec::new()) }
    }

    /// Returns a proxy for the root of the Blobfs filesystem.  This can be called before Blobfs is
    /// mounted and it will get routed once Blobfs is mounted.
    pub fn blobfs_root(&mut self) -> Result<fio::DirectoryProxy, Error> {
        self.blobfs.root()
    }

    /// Returns a proxy for the root of the data filesystem.  This can be called before Blobfs is
    /// mounted and it will get routed once Blobfs is mounted.
    pub fn data_root(&mut self) -> Result<fio::DirectoryProxy, Error> {
        self.data.root()
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

    async fn mount_data(&mut self, device: &mut dyn Device) -> Result<(), Error> {
        let mut fs = Fxfs::from_channel(device.proxy()?.into_channel().unwrap().into())?;
        let mut serving_fs = None;
        let vol = match fs.serve_multi_volume().await {
            Ok(fs) => {
                serving_fs = Some(fs);
                fxfs::unlock_data_volume(serving_fs.as_mut().unwrap()).await
            }
            Err(e) => Err(e),
        };
        let vol = match vol {
            Ok(vol) => vol,
            Err(e) => {
                log::info!("Failed to mount data partition, reformatting: {}", e);
                // TODO(fxbug.dev/102666): We need to ensure the hardware key source is also wiped.
                let _ = serving_fs.take();
                fs.format().await?;
                serving_fs = Some(fs.serve_multi_volume().await?);
                fxfs::init_data_volume(serving_fs.as_mut().unwrap()).await?
            }
        };
        if let Filesystem::Queue(queue) = &mut self.data {
            let root_dir = vol.root();
            for server in queue.drain(..) {
                root_dir.clone(fio::OpenFlags::CLONE_SAME_RIGHTS, server.into_channel().into())?;
            }
        } else {
            panic!("data already mounted");
        };
        self.data = Filesystem::ServingMultiVolume(serving_fs.unwrap());
        Ok(())
    }
}
