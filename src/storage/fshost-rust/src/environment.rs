// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::device::Device,
    anyhow::Error,
    async_trait::async_trait,
    fidl::endpoints::{create_proxy, Proxy, ServerEnd},
    fidl_fuchsia_device::ControllerProxy,
    fidl_fuchsia_fxfs::CryptMarker,
    fidl_fuchsia_io as fio,
    fs_management::{
        filesystem::{ServingMultiVolumeFilesystem, ServingSingleVolumeFilesystem},
        Blobfs, Fxfs,
    },
    fuchsia_component::client::connect_to_protocol,
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
        let crypt_service = Some(
            connect_to_protocol::<CryptMarker>()
                .expect("Unable to connect to Crypt service")
                .into_channel()
                .unwrap()
                .into_zx_channel()
                .into(),
        );
        let mut serving_fs;
        let vol = match fs.serve_multi_volume().await {
            Ok(fs) => {
                serving_fs = fs;
                serving_fs.open_volume("default", crypt_service).await?
            }
            Err(e) => {
                log::info!("Failed to mount data partition, reformating: {}", e);
                fs.format().await?;
                serving_fs = fs.serve_multi_volume().await?;
                serving_fs.create_volume("default", crypt_service).await?
            }
        };
        // For now, we expect the crypt service to be initialised with the appropriate keys.
        if let Filesystem::Queue(queue) = &mut self.data {
            let root_dir = vol.root();
            for server in queue.drain(..) {
                root_dir.clone(fio::OpenFlags::CLONE_SAME_RIGHTS, server.into_channel().into())?;
            }
        } else {
            panic!("data already mounted");
        };
        self.data = Filesystem::ServingMultiVolume(serving_fs);
        Ok(())
    }
}
