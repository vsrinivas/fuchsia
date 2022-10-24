// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        boot_args::BootArgs,
        crypt::{fxfs, zxcrypt},
        device::Device,
    },
    anyhow::{anyhow, Context, Error},
    async_trait::async_trait,
    fidl::endpoints::{create_proxy, Proxy, ServerEnd},
    fidl_fuchsia_device::ControllerProxy,
    fidl_fuchsia_hardware_block_partition::Guid,
    fidl_fuchsia_hardware_block_volume::VolumeManagerMarker,
    fidl_fuchsia_io as fio,
    fs_management::{
        filesystem::{ServingMultiVolumeFilesystem, ServingSingleVolumeFilesystem},
        Blobfs, Fxfs, Minfs,
    },
    fuchsia_component::client::connect_to_protocol_at_path,
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

    /// Bind zxcrypt to this device, unsealing it and formatting it if necessary.
    async fn bind_zxcrypt(&mut self, device: &mut dyn Device) -> Result<(), Error>;

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
    boot_args: &'a BootArgs,
    data: Filesystem,
}

impl<'a> FshostEnvironment<'a> {
    pub fn new(config: &'a fshost_config::Config, boot_args: &'a BootArgs) -> Self {
        Self {
            config,
            blobfs: Filesystem::Queue(Vec::new()),
            boot_args,
            data: Filesystem::Queue(Vec::new()),
        }
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

    async fn bind_zxcrypt(&mut self, device: &mut dyn Device) -> Result<(), Error> {
        self.attach_driver(device, "zxcrypt.so").await?;
        zxcrypt::unseal_or_format(device).await
    }

    async fn mount_blobfs(&mut self, device: &mut dyn Device) -> Result<(), Error> {
        let queue = self.blobfs.queue().ok_or(anyhow!("blobfs already mounted"))?;

        // Setting max partition size for blobfs
        if let Err(e) = set_partition_max_size(device, self.config.blobfs_max_bytes).await {
            tracing::warn!("Failed to set max partition size for blobfs: {:?}", e);
        };

        let mut blobfs = Blobfs::default();
        if let Some(compression) = self.boot_args.blobfs_write_compression_algorithm() {
            blobfs.blob_compression = Some(compression);
        }
        if let Some(eviction) = self.boot_args.blobfs_eviction_policy() {
            blobfs.blob_eviction_policy = Some(eviction);
        }
        let fs = fs_management::filesystem::Filesystem::from_channel(
            device.proxy()?.into_channel().unwrap().into(),
            blobfs,
        )?
        .serve()
        .await?;
        let root_dir = fs.root();
        for server in queue.drain(..) {
            root_dir.clone(fio::OpenFlags::CLONE_SAME_RIGHTS, server.into_channel().into())?;
        }
        self.blobfs = Filesystem::Serving(fs);
        Ok(())
    }

    async fn mount_data(&mut self, device: &mut dyn Device) -> Result<(), Error> {
        let _ = self.data.queue().ok_or_else(|| anyhow!("data partition already mounted"))?;

        // Setting max partition size for data
        if let Err(e) = set_partition_max_size(device, self.config.data_max_bytes).await {
            tracing::warn!("Failed to set max partition size for data: {:?}", e);
        };

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
                let proxy = if self.config.no_zxcrypt || self.config.fvm_ramdisk {
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

async fn set_partition_max_size(device: &mut dyn Device, max_byte_size: u64) -> Result<(), Error> {
    let index =
        device.topological_path().find("/fvm").ok_or(anyhow!("fvm is not in the device path"))?;
    // The 4 is from the 4 characters in "/fvm"
    let fvm_path = &device.topological_path()[..index + 4];

    let fvm_proxy = connect_to_protocol_at_path::<VolumeManagerMarker>(&fvm_path)
        .context("Failed to connect to fvm volume manager")?;
    let (status, info) = fvm_proxy.get_info().await.context("Transport error in get_info call")?;
    zx::Status::ok(status).context("get_info call failed")?;
    let info = info.ok_or(anyhow!("Expected info"))?;
    let slice_size = info.slice_size;
    let max_slice_count = max_byte_size / slice_size;
    let mut instance_guid =
        Guid { value: *device.partition_instance().await.context("Expected partition instance")? };
    let status = fvm_proxy
        .set_partition_limit(&mut instance_guid, max_slice_count)
        .await
        .context("Transport error on set_partition_limit")?;
    zx::Status::ok(status).context("set_partition_limit failed")?;
    Ok(())
}
