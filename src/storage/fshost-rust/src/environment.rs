// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        crypt::{fxfs, zxcrypt},
        device::Device,
    },
    anyhow::{anyhow, Error},
    async_trait::async_trait,
    fidl::endpoints::{create_proxy, Proxy, ServerEnd},
    fidl_fuchsia_device::ControllerProxy,
    fidl_fuchsia_io as fio,
    fs_management::{
        filesystem::{ServingMultiVolumeFilesystem, ServingSingleVolumeFilesystem},
        Blobfs, Fxfs, Minfs,
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
                .volume("data")
                .ok_or(anyhow!("no data volume"))?
                .root()
                .clone(fio::OpenFlags::CLONE_SAME_RIGHTS, server.into_channel().into())?,
        }
        Ok(proxy)
    }

    fn queue(&mut self) -> Option<&mut Vec<ServerEnd<fio::DirectoryMarker>>> {
        match self {
            Filesystem::Queue(queue) => Some(queue),
            _ => None,
        }
    }
}

/// Implements the Environment trait and keeps track of mounted filesystems.
pub struct FshostEnvironment<'a> {
    config: &'a fshost_config::Config,
    blobfs: Filesystem,
    data: Filesystem,
}

impl<'a> FshostEnvironment<'a> {
    pub fn new(config: &'a fshost_config::Config) -> Self {
        Self { config, blobfs: Filesystem::Queue(Vec::new()), data: Filesystem::Queue(Vec::new()) }
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
impl<'a> Environment for FshostEnvironment<'a> {
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
        let queue = self.blobfs.queue().ok_or(anyhow!("blobfs already mounted"))?;

        let fs =
            Blobfs::from_channel(device.proxy()?.into_channel().unwrap().into())?.serve().await?;
        let root_dir = fs.root();
        for server in queue.drain(..) {
            root_dir.clone(fio::OpenFlags::CLONE_SAME_RIGHTS, server.into_channel().into())?;
        }
        self.blobfs = Filesystem::Serving(fs);
        Ok(())
    }

    async fn mount_data(&mut self, device: &mut dyn Device) -> Result<(), Error> {
        let _ = self.data.queue().ok_or_else(|| anyhow!("data partition already mounted"))?;

        let mut filesystem = match self.config.data_filesystem_format.as_ref() {
            "fxfs" => {
                let mut fs = Fxfs::from_channel(device.proxy()?.into_channel().unwrap().into())?;
                let mut serving_fs = None;
                let vol = match fs.serve_multi_volume().await {
                    Ok(fs) => {
                        serving_fs = Some(fs);
                        fxfs::unlock_data_volume(serving_fs.as_mut().unwrap()).await
                    }
                    Err(e) => Err(e),
                };
                let _ = match vol {
                    Ok(vol) => vol,
                    Err(e) => {
                        tracing::info!("Failed to mount data partition, reformatting: {}", e);
                        // TODO(fxbug.dev/102666): We need to ensure the hardware key source is
                        // also wiped.
                        let _ = serving_fs.take();
                        fs.format().await?;
                        serving_fs = Some(fs.serve_multi_volume().await?);
                        fxfs::init_data_volume(serving_fs.as_mut().unwrap()).await?
                    }
                };
                Filesystem::ServingMultiVolume(serving_fs.unwrap())
            }
            // Default to minfs
            _ => {
                let proxy = if self.config.no_zxcrypt {
                    device.proxy()?
                } else {
                    self.attach_driver(device, "zxcrypt.so").await?;
                    zxcrypt::unseal_or_format(device).await?;

                    // Instead of waiting for the zxcrypt device to go through the watcher and then
                    // matching it again, just wait for it to appear and immediately use it. The
                    // block watcher will find the zxcrypt device later and pass it through the
                    // matchers, but it won't match anything since the fvm matcher only matches
                    // immediate children.
                    device.get_child("/zxcrypt/unsealed/block").await?.proxy()?
                };

                let mut fs = Minfs::from_channel(proxy.into_channel().unwrap().into())?;
                Filesystem::Serving(match fs.serve().await {
                    Ok(fs) => fs,
                    Err(e) => {
                        tracing::info!("Failed to mount data partition, reformatting: {}", e);
                        fs.format().await?;
                        fs.serve().await?
                    }
                })
            }
        };

        let queue = self.data.queue().unwrap();
        let root_dir = filesystem.root()?;
        for server in queue.drain(..) {
            root_dir.clone(fio::OpenFlags::CLONE_SAME_RIGHTS, server.into_channel().into())?;
        }
        self.data = filesystem;
        Ok(())
    }
}
