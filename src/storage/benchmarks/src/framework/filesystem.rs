// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::framework::{BlockDevice, BlockDeviceConfig, BlockDeviceFactory},
    async_trait::async_trait,
    either::Either,
    fidl::encoding::Decodable,
    fidl_fuchsia_fs::AdminMarker,
    fidl_fuchsia_fs_startup::{StartOptions, StartupMarker},
    fidl_fuchsia_fxfs::{CryptManagementMarker, CryptMarker, KeyPurpose},
    fidl_fuchsia_hardware_block::BlockMarker,
    fidl_fuchsia_io as fio,
    fs_management::{
        filesystem::{ServingMultiVolumeFilesystem, ServingSingleVolumeFilesystem},
        Blobfs, F2fs, FSConfig, Fxfs, Minfs,
    },
    fuchsia_component::client::{
        connect_channel_to_protocol, connect_to_childs_protocol, open_childs_exposed_directory,
    },
    fuchsia_zircon as zx,
    std::{
        path::Path,
        sync::{Arc, Once},
    },
};

const MOUNT_PATH: &str = "/benchmark";

/// Which filesystem to run a benchmark against.
#[derive(Clone, Copy)]
#[allow(dead_code)] // Blobfs is not currently used in any benchmarks.
pub enum FilesystemConfig {
    Blobfs,
    Fxfs,
    F2fs,
    Memfs,
    Minfs,
}

impl FilesystemConfig {
    pub async fn start_filesystem<BDF: BlockDeviceFactory>(
        &self,
        block_device_factory: &BDF,
    ) -> Box<dyn Filesystem> {
        match self {
            Self::Blobfs => Box::new(create_blobfs(block_device_factory).await),
            Self::Fxfs => Box::new(create_fxfs(block_device_factory).await),
            Self::F2fs => Box::new(create_f2fs(block_device_factory).await),
            Self::Memfs => Box::new(Memfs::new().await),
            Self::Minfs => Box::new(create_minfs(block_device_factory).await),
        }
    }

    pub fn name(&self) -> String {
        match self {
            Self::Blobfs => "blobfs".to_owned(),
            Self::Fxfs => "fxfs".to_owned(),
            Self::F2fs => "f2fs".to_owned(),
            Self::Memfs => "memfs".to_owned(),
            Self::Minfs => "minfs".to_owned(),
        }
    }
}

/// A trait representing a mounted filesystem.
#[async_trait]
pub trait Filesystem: Send {
    /// Clears all cached files in the filesystem. This method is used in "cold" benchmarks to
    /// ensure that the filesystem isn't using cached data from the setup phase in the benchmark
    /// phase.
    async fn clear_cache(&mut self);

    async fn shutdown(self: Box<Self>);

    fn mount_point(&self) -> &Path {
        Path::new(MOUNT_PATH)
    }
}

struct FsmFilesystem<FSC: FSConfig + Send + Sync, BD: BlockDevice + Send> {
    fs: fs_management::filesystem::Filesystem<FSC>,
    serving_filesystem: Option<Either<ServingSingleVolumeFilesystem, ServingMultiVolumeFilesystem>>,
    _block_device: BD,
}

impl<FSC: FSConfig + Send + Sync, BD: BlockDevice + Send> FsmFilesystem<FSC, BD> {
    pub async fn new(config: FSC, block_device: BD) -> Self {
        let mut fs =
            fs_management::filesystem::Filesystem::from_node(block_device.get_node(), config);
        fs.format().await.expect("Failed to format the filesystem");
        let serving_filesystem = if fs.config().is_multi_volume() {
            let mut serving_filesystem =
                fs.serve_multi_volume().await.expect("Failed to start the filesystem");
            let vol = serving_filesystem
                .create_volume("default", fs.config().crypt_client().map(|c| c.into()))
                .await
                .expect("Failed to create volume");
            vol.bind_to_path(MOUNT_PATH).expect("Failed to bind the volume");
            Either::Right(serving_filesystem)
        } else {
            let mut serving_filesystem = fs.serve().await.expect("Failed to start the filesystem");
            serving_filesystem.bind_to_path(MOUNT_PATH).expect("Failed to bind the filesystem");
            Either::Left(serving_filesystem)
        };
        Self { fs, serving_filesystem: Some(serving_filesystem), _block_device: block_device }
    }
}

#[async_trait]
impl<FSC: FSConfig + Send + Sync, BD: BlockDevice + Send> Filesystem for FsmFilesystem<FSC, BD> {
    async fn clear_cache(&mut self) {
        // Remount the filesystem to guarantee that all cached data from reads and write is cleared.
        let serving_filesystem = self.serving_filesystem.take().unwrap();
        let serving_filesystem = match serving_filesystem {
            Either::Left(serving_filesystem) => {
                serving_filesystem.shutdown().await.expect("Failed to stop the filesystem");
                let mut serving_filesystem =
                    self.fs.serve().await.expect("Failed to start the filesystem");
                serving_filesystem.bind_to_path(MOUNT_PATH).expect("Failed to bind the filesystem");
                Either::Left(serving_filesystem)
            }
            Either::Right(serving_filesystem) => {
                serving_filesystem.shutdown().await.expect("Failed to stop the filesystem");
                let mut serving_filesystem =
                    self.fs.serve_multi_volume().await.expect("Failed to start the filesystem");
                let vol = serving_filesystem
                    .open_volume("default", self.fs.config().crypt_client().map(|c| c.into()))
                    .await
                    .expect("Failed to create volume");
                vol.bind_to_path(MOUNT_PATH).expect("Failed to bind the volume");
                Either::Right(serving_filesystem)
            }
        };
        self.serving_filesystem = Some(serving_filesystem);
    }

    async fn shutdown(mut self: Box<Self>) {
        if let Some(fs) = self.serving_filesystem {
            match fs {
                Either::Left(fs) => fs.shutdown().await.expect("Failed to stop filesystem"),
                Either::Right(fs) => fs.shutdown().await.expect("Failed to stop filesystem"),
            }
        }
    }
}

async fn create_blobfs<BDF: BlockDeviceFactory>(
    block_device_factory: &BDF,
) -> FsmFilesystem<Blobfs, <BDF as BlockDeviceFactory>::BlockDevice> {
    let block_device = block_device_factory
        .create_block_device(&BlockDeviceConfig { use_zxcrypt: false, fvm_volume_size: None })
        .await;
    FsmFilesystem::new(Blobfs::default(), block_device).await
}

fn get_crypt_client() -> zx::Channel {
    static CRYPT_CLIENT_INITIALIZER: Once = Once::new();
    CRYPT_CLIENT_INITIALIZER.call_once(|| {
        let (client_end, server_end) = zx::Channel::create().unwrap();
        connect_channel_to_protocol::<CryptManagementMarker>(server_end)
            .expect("Failed to connect to the crypt management service");
        let crypt_management_service =
            fidl_fuchsia_fxfs::CryptManagementSynchronousProxy::new(client_end);

        let mut key = [0; 32];
        zx::cprng_draw(&mut key);
        match crypt_management_service
            .add_wrapping_key(0, &key, zx::Time::INFINITE)
            .expect("FIDL failed")
            .map_err(zx::Status::from_raw)
        {
            Ok(()) => {}
            Err(zx::Status::ALREADY_EXISTS) => {
                // In tests, the binary is run multiple times which gets around the `Once`. The fxfs
                // crypt component is not restarted for each test so it may already be initialized.
                return;
            }
            Err(e) => panic!("add_wrapping_key failed: {:?}", e),
        };
        zx::cprng_draw(&mut key);
        crypt_management_service
            .add_wrapping_key(1, &key, zx::Time::INFINITE)
            .expect("FIDL failed")
            .map_err(zx::Status::from_raw)
            .expect("add_wrapping_key failed");
        crypt_management_service
            .set_active_key(KeyPurpose::Data, 0, zx::Time::INFINITE)
            .expect("FIDL failed")
            .map_err(zx::Status::from_raw)
            .expect("set_active_key failed");
        crypt_management_service
            .set_active_key(KeyPurpose::Metadata, 1, zx::Time::INFINITE)
            .expect("FIDL failed")
            .map_err(zx::Status::from_raw)
            .expect("set_active_key failed");
    });
    let (client_end, server_end) = zx::Channel::create().unwrap();
    connect_channel_to_protocol::<CryptMarker>(server_end)
        .expect("Failed to connect to crypt service");
    client_end
}

async fn create_fxfs<BDF: BlockDeviceFactory>(
    block_device_factory: &BDF,
) -> FsmFilesystem<Fxfs, <BDF as BlockDeviceFactory>::BlockDevice> {
    let block_device = block_device_factory
        .create_block_device(&BlockDeviceConfig {
            use_zxcrypt: false,
            fvm_volume_size: Some(60 * 1024 * 1024),
        })
        .await;
    FsmFilesystem::new(Fxfs::with_crypt_client(Arc::new(get_crypt_client)), block_device).await
}

async fn create_f2fs<BDF: BlockDeviceFactory>(
    block_device_factory: &BDF,
) -> FsmFilesystem<F2fs, <BDF as BlockDeviceFactory>::BlockDevice> {
    let block_device = block_device_factory
        .create_block_device(&BlockDeviceConfig {
            use_zxcrypt: true,
            fvm_volume_size: Some(60 * 1024 * 1024),
        })
        .await;
    FsmFilesystem::new(F2fs::default(), block_device).await
}

pub struct Memfs {}

impl Memfs {
    pub async fn new() -> Self {
        let startup = connect_to_childs_protocol::<StartupMarker>("memfs".to_string(), None)
            .await
            .expect("Failed to connect to memfs");
        let mut options = StartOptions::new_empty();
        // Memfs doesn't need a block device but FIDL prevents passing an invalid handle.
        let (device_client_end, _) = fidl::endpoints::create_endpoints::<BlockMarker>().unwrap();
        startup
            .start(device_client_end, &mut options)
            .await
            .unwrap()
            .map_err(zx::Status::from_raw)
            .unwrap();

        let exposed_dir = open_childs_exposed_directory("memfs", None)
            .await
            .expect("Failed to connect to memfs's exposed directory");

        let (root_dir, server_end) = fidl::endpoints::create_endpoints().unwrap();
        exposed_dir
            .open(
                fio::OpenFlags::RIGHT_READABLE
                    | fio::OpenFlags::POSIX_EXECUTABLE
                    | fio::OpenFlags::POSIX_WRITABLE,
                0,
                "root",
                fidl::endpoints::ServerEnd::new(server_end.into_channel()),
            )
            .expect("Failed to open memfs's root");
        let namespace = fdio::Namespace::installed().expect("Failed to get local namespace");
        namespace.bind(MOUNT_PATH, root_dir).expect("Failed to bind memfs");

        Self {}
    }
}

#[async_trait]
impl Filesystem for Memfs {
    async fn clear_cache(&mut self) {}

    async fn shutdown(mut self: Box<Self>) {
        let admin = connect_to_childs_protocol::<AdminMarker>("memfs".to_string(), None)
            .await
            .expect("Failed to connect to memfs Admin");
        admin.shutdown().await.expect("Failed to shutdown memfs");

        let namespace = fdio::Namespace::installed().expect("Failed to get local namespace");
        namespace.unbind(MOUNT_PATH).expect("Failed to unbind memfs");
    }
}

async fn create_minfs<BDF: BlockDeviceFactory>(
    block_device_factory: &BDF,
) -> FsmFilesystem<Minfs, <BDF as BlockDeviceFactory>::BlockDevice> {
    let block_device = block_device_factory
        .create_block_device(&BlockDeviceConfig { use_zxcrypt: true, fvm_volume_size: None })
        .await;
    FsmFilesystem::new(Minfs::default(), block_device).await
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::framework::RamdiskFactory,
        std::{
            fs::OpenOptions,
            io::{Read, Write},
        },
    };
    const DEVICE_SIZE: u64 = 64 * 1024 * 1024;
    const BLOCK_SIZE: u64 = 4 * 1024;
    const BLOCK_COUNT: u64 = DEVICE_SIZE / BLOCK_SIZE;

    #[fuchsia::test]
    async fn start_blobfs() {
        const BLOB_NAME: &str = "bd905f783ceae4c5ba8319703d7505ab363733c2db04c52c8405603a02922b15";
        const BLOB_CONTENTS: &str = "blob-contents";

        let ramdisk_factory = RamdiskFactory::new(BLOCK_SIZE, BLOCK_COUNT).await;
        let mut fs = FilesystemConfig::Blobfs.start_filesystem(&ramdisk_factory).await;
        let blob_path = fs.mount_point().join(BLOB_NAME);

        {
            let mut file = OpenOptions::new()
                .create_new(true)
                .write(true)
                .truncate(true)
                .open(&blob_path)
                .unwrap();
            file.set_len(BLOB_CONTENTS.len() as u64).unwrap();
            file.write_all(BLOB_CONTENTS.as_bytes()).unwrap();
        }
        fs.clear_cache().await;
        {
            let mut file = OpenOptions::new().read(true).open(&blob_path).unwrap();
            let mut buf = [0u8; BLOB_CONTENTS.len()];
            file.read_exact(&mut buf).unwrap();
            assert_eq!(std::str::from_utf8(&buf).unwrap(), BLOB_CONTENTS);
        }

        fs.shutdown().await;
    }

    async fn check_filesystem(config: FilesystemConfig) {
        const FILE_CONTENTS: &str = "file-contents";

        let ramdisk_factory = RamdiskFactory::new(BLOCK_SIZE, BLOCK_COUNT).await;
        let mut fs = config.start_filesystem(&ramdisk_factory).await;

        let file_path = fs.mount_point().join("filename");
        {
            let mut file =
                OpenOptions::new().create_new(true).write(true).open(&file_path).unwrap();
            file.write_all(FILE_CONTENTS.as_bytes()).unwrap();
        }
        fs.clear_cache().await;
        {
            let mut file = OpenOptions::new().read(true).open(&file_path).unwrap();
            let mut buf = [0u8; FILE_CONTENTS.len()];
            file.read_exact(&mut buf).unwrap();
            assert_eq!(std::str::from_utf8(&buf).unwrap(), FILE_CONTENTS);
        }
        fs.shutdown().await;
    }

    #[fuchsia::test]
    async fn start_fxfs() {
        check_filesystem(FilesystemConfig::Fxfs).await;
    }

    #[fuchsia::test]
    async fn start_f2fs() {
        check_filesystem(FilesystemConfig::F2fs).await;
    }

    #[fuchsia::test]
    async fn start_minfs() {
        check_filesystem(FilesystemConfig::Minfs).await;
    }

    #[fuchsia::test]
    async fn start_memfs() {
        check_filesystem(FilesystemConfig::Memfs).await;
    }
}
