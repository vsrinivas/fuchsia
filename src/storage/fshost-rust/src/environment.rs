// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        boot_args::BootArgs,
        crypt::{fxfs, zxcrypt},
        device::{constants::DEFAULT_F2FS_MIN_BYTES, Device},
        volume::resize_volume,
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
        format::DiskFormat,
        Blobfs, F2fs, FSConfig, Fxfs, Minfs,
    },
    fuchsia_component::client::connect_to_protocol_at_path,
    fuchsia_zircon as zx,
    std::sync::Arc,
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
    ServingMultiVolume(ServingMultiVolumeFilesystem, String),
}

impl Filesystem {
    fn root(&mut self) -> Result<fio::DirectoryProxy, Error> {
        let (proxy, server) = create_proxy::<fio::DirectoryMarker>()?;
        match self {
            Filesystem::Queue(queue) => queue.push(server),
            Filesystem::Serving(fs) => {
                fs.root().clone(fio::OpenFlags::CLONE_SAME_RIGHTS, server.into_channel().into())?
            }
            Filesystem::ServingMultiVolume(fs, data_volume_name) => fs
                .volume(&data_volume_name)
                .ok_or(anyhow!("data volume {} not found", data_volume_name))?
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
    config: Arc<fshost_config::Config>,
    blobfs: Filesystem,
    boot_args: &'a BootArgs,
    data: Filesystem,
}

impl<'a> FshostEnvironment<'a> {
    pub fn new(config: &Arc<fshost_config::Config>, boot_args: &'a BootArgs) -> Self {
        Self {
            config: config.clone(),
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
        tracing::info!(path = %device.topological_path(), %driver_path, "Binding driver to device");
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

        tracing::info!(path = %device.topological_path(), "Mounting /blob");

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

        let mut filesystem = match self.config.data_filesystem_format.as_ref() {
            "fxfs" => self.serve_data(device, Fxfs::default()).await?,
            "f2fs" => self.serve_data(device, F2fs::default()).await?,
            _ => self.serve_data(device, Minfs::default()).await?,
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

impl<'a> FshostEnvironment<'a> {
    async fn serve_data<FSC: FSConfig>(
        &mut self,
        device: &mut dyn Device,
        config: FSC,
    ) -> Result<Filesystem, Error> {
        let format = config.disk_format();
        tracing::info!(
            path = %device.topological_path(),
            expected_format = ?format,
            "Mounting /data"
        );

        // Set the max partition size for data
        if let Err(e) = set_partition_max_size(device, self.config.data_max_bytes).await {
            tracing::warn!(?e, "Failed to set max partition size for data");
        };

        let mut new_dev;
        let mut inside_zxcrypt = false;
        let device = match format {
            // Fxfs never has zxcrypt underneath
            DiskFormat::Fxfs => device,
            // Skip zxcrypt in these configurations.
            _ if self.config.no_zxcrypt => device,
            _ if self.config.fvm_ramdisk => device,
            // Otherwise, we need to bind a zxcrypt device first.
            _ => {
                inside_zxcrypt = true;
                self.bind_zxcrypt(device).await?;

                // Instead of waiting for the zxcrypt device to go through the watcher and then
                // matching it again, just wait for it to appear and immediately use it. The
                // block watcher will find the zxcrypt device later and pass it through the
                // matchers, but it won't match anything since the fvm matcher only matches
                // immediate children.
                new_dev = device.get_child("/zxcrypt/unsealed/block").await?;
                new_dev.as_mut()
            }
        };

        let detected_format = device.content_format().await?;
        let volume_proxy = fidl_fuchsia_hardware_block_volume::VolumeProxy::from_channel(
            device.proxy()?.into_channel().unwrap(),
        );
        let mut fs = fs_management::filesystem::Filesystem::from_channel(
            device.proxy()?.into_channel().unwrap().into(),
            config,
        )?;

        let mut reformatted = false;
        if detected_format != format {
            tracing::info!(
                ?detected_format,
                expected_format = ?format,
                "Expected format not detected. Reformatting.",
            );
            self.format_data(&mut fs, volume_proxy, inside_zxcrypt).await?;
            reformatted = true;
        } else if self.config.check_filesystems {
            tracing::info!(?format, "fsck started");
            if let Err(error) = fs.fsck().await {
                tracing::error!(?format, ?error, "FILESYSTEM CORRUPTION DETECTED!");
                tracing::error!(
                    "Please file a bug to the Storage component in http://fxbug.dev, including a\
                    device snapshot collected with `ffx target snapshot` if possible.",
                );

                // TODO(fxbug.dev/109290): file a crash report

                if !self.config.format_data_on_corruption {
                    tracing::error!(?format, "format on corruption is disabled, not continuing");
                    return Err(error);
                }
                self.format_data(&mut fs, volume_proxy, inside_zxcrypt).await?;
                reformatted = true;
            } else {
                tracing::info!(?format, "fsck completed OK");
            }
        }

        Ok(match format {
            DiskFormat::Fxfs => {
                let mut serving_fs = fs.serve_multi_volume().await?;
                let (volume_name, _) = if reformatted {
                    fxfs::init_data_volume(&mut serving_fs, &self.config).await?
                } else {
                    fxfs::unlock_data_volume(&mut serving_fs, &self.config).await?
                };
                Filesystem::ServingMultiVolume(serving_fs, volume_name)
            }
            _ => Filesystem::Serving(fs.serve().await?),
        })
    }

    async fn format_data<FSC: FSConfig>(
        &mut self,
        fs: &mut fs_management::filesystem::Filesystem<FSC>,
        volume_proxy: fidl_fuchsia_hardware_block_volume::VolumeProxy,
        inside_zxcrypt: bool,
    ) -> Result<(), Error> {
        let format = fs.config().disk_format();
        tracing::info!(?format, "Formatting");
        match format {
            DiskFormat::Fxfs => {
                let target_bytes = self.config.data_max_bytes;
                tracing::info!(target_bytes, "Resizing data volume");
                let allocated_bytes = resize_volume(&volume_proxy, target_bytes, inside_zxcrypt)
                    .await
                    .context("format volume resize")?;
                if allocated_bytes < target_bytes {
                    tracing::warn!(
                        target_bytes,
                        allocated_bytes,
                        "Allocated less space than desired"
                    );
                }
            }
            DiskFormat::F2fs => {
                let target_bytes = self.config.data_max_bytes;
                let (status, info, _) =
                    volume_proxy.get_volume_info().await.context("volume get_info call failed")?;
                zx::Status::ok(status).context("volume get_info returned an error")?;
                let info = info.ok_or_else(|| anyhow!("volume get_info returned no info"))?;
                let slice_size = info.slice_size;
                let round_up = |val: u64, divisor: u64| ((val + (divisor - 1)) / divisor) * divisor;
                let mut required_size = round_up(DEFAULT_F2FS_MIN_BYTES, slice_size);
                if inside_zxcrypt {
                    required_size += slice_size;
                }

                let target_bytes = std::cmp::max(target_bytes, required_size);
                tracing::info!(target_bytes, "Resizing data volume");
                let allocated_bytes = resize_volume(&volume_proxy, target_bytes, inside_zxcrypt)
                    .await
                    .context("format volume resize")?;
                if allocated_bytes < DEFAULT_F2FS_MIN_BYTES {
                    tracing::error!(
                        minimum_bytes = DEFAULT_F2FS_MIN_BYTES,
                        allocated_bytes,
                        "Not enough space for f2fs"
                    )
                }
                if allocated_bytes < target_bytes {
                    tracing::warn!(
                        target_bytes,
                        allocated_bytes,
                        "Allocated less space than desired"
                    );
                }
            }
            _ => (),
        }

        fs.format().await
    }
}

async fn set_partition_max_size(device: &mut dyn Device, max_byte_size: u64) -> Result<(), Error> {
    if max_byte_size == 0 {
        return Ok(());
    }

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
