// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
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
        FSConfig,
    },
    fuchsia_component::client::{
        connect_channel_to_protocol, connect_to_childs_protocol, open_childs_exposed_directory,
    },
    fuchsia_zircon as zx,
    std::{
        path::Path,
        sync::{Arc, Once},
    },
    storage_benchmarks::{
        BlockDevice, BlockDeviceConfig, BlockDeviceFactory, Filesystem, FilesystemConfig,
    },
};

const MOUNT_PATH: &str = "/benchmark";

struct FsmFilesystem<FSC: FSConfig + Send + Sync> {
    fs: fs_management::filesystem::Filesystem<FSC>,
    serving_filesystem: Option<Either<ServingSingleVolumeFilesystem, ServingMultiVolumeFilesystem>>,
    _block_device: Box<dyn BlockDevice>,
}

impl<FSC: FSConfig + Send + Sync> FsmFilesystem<FSC> {
    pub async fn new(config: FSC, block_device: Box<dyn BlockDevice>) -> Self {
        let mut fs = fs_management::filesystem::Filesystem::from_path(
            block_device.get_path().to_str().unwrap(),
            config,
        )
        .unwrap();
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
impl<FSC: FSConfig + Send + Sync> Filesystem for FsmFilesystem<FSC> {
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

    fn benchmark_dir(&self) -> &Path {
        Path::new(MOUNT_PATH)
    }
}

pub struct Blobfs {}

impl Blobfs {
    #[allow(dead_code)] // Blobfs is not currently used in any benchmarks.
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl FilesystemConfig for Blobfs {
    async fn start_filesystem(
        &self,
        block_device_factory: &dyn BlockDeviceFactory,
    ) -> Box<dyn Filesystem> {
        let block_device = block_device_factory
            .create_block_device(&BlockDeviceConfig { use_zxcrypt: false, fvm_volume_size: None })
            .await;
        Box::new(FsmFilesystem::new(fs_management::Blobfs::default(), block_device).await)
    }

    fn name(&self) -> String {
        "blobfs".to_owned()
    }
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

pub struct Fxfs {}

impl Fxfs {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl FilesystemConfig for Fxfs {
    async fn start_filesystem(
        &self,
        block_device_factory: &dyn BlockDeviceFactory,
    ) -> Box<dyn Filesystem> {
        let block_device = block_device_factory
            .create_block_device(&BlockDeviceConfig {
                use_zxcrypt: false,
                fvm_volume_size: Some(60 * 1024 * 1024),
            })
            .await;
        let fxfs = FsmFilesystem::new(
            fs_management::Fxfs::with_crypt_client(Arc::new(get_crypt_client)),
            block_device,
        )
        .await;
        Box::new(fxfs)
    }

    fn name(&self) -> String {
        "fxfs".to_owned()
    }
}

pub struct F2fs {}

impl F2fs {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl FilesystemConfig for F2fs {
    async fn start_filesystem(
        &self,
        block_device_factory: &dyn BlockDeviceFactory,
    ) -> Box<dyn Filesystem> {
        let block_device = block_device_factory
            .create_block_device(&BlockDeviceConfig {
                use_zxcrypt: true,
                fvm_volume_size: Some(60 * 1024 * 1024),
            })
            .await;
        Box::new(FsmFilesystem::new(fs_management::F2fs::default(), block_device).await)
    }

    fn name(&self) -> String {
        "f2fs".to_owned()
    }
}

struct MemfsInstance {}

impl MemfsInstance {
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
impl Filesystem for MemfsInstance {
    async fn clear_cache(&mut self) {}

    async fn shutdown(mut self: Box<Self>) {
        let admin = connect_to_childs_protocol::<AdminMarker>("memfs".to_string(), None)
            .await
            .expect("Failed to connect to memfs Admin");
        admin.shutdown().await.expect("Failed to shutdown memfs");

        let namespace = fdio::Namespace::installed().expect("Failed to get local namespace");
        namespace.unbind(MOUNT_PATH).expect("Failed to unbind memfs");
    }

    fn benchmark_dir(&self) -> &Path {
        Path::new(MOUNT_PATH)
    }
}

pub struct Memfs {}

impl Memfs {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl FilesystemConfig for Memfs {
    async fn start_filesystem(
        &self,
        _block_device_factory: &dyn BlockDeviceFactory,
    ) -> Box<dyn Filesystem> {
        Box::new(MemfsInstance::new().await)
    }

    fn name(&self) -> String {
        "memfs".to_owned()
    }
}

pub struct Minfs {}

impl Minfs {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl FilesystemConfig for Minfs {
    async fn start_filesystem(
        &self,
        block_device_factory: &dyn BlockDeviceFactory,
    ) -> Box<dyn Filesystem> {
        let block_device = block_device_factory
            .create_block_device(&BlockDeviceConfig { use_zxcrypt: true, fvm_volume_size: None })
            .await;
        Box::new(FsmFilesystem::new(fs_management::Minfs::default(), block_device).await)
    }

    fn name(&self) -> String {
        "minfs".to_owned()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::block_devices::RamdiskFactory,
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
        let mut fs = Blobfs::new().start_filesystem(&ramdisk_factory).await;
        let blob_path = fs.benchmark_dir().join(BLOB_NAME);

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

    async fn check_filesystem(filesystem: &dyn FilesystemConfig) {
        const FILE_CONTENTS: &str = "file-contents";

        let ramdisk_factory = RamdiskFactory::new(BLOCK_SIZE, BLOCK_COUNT).await;
        let mut fs = filesystem.start_filesystem(&ramdisk_factory).await;

        let file_path = fs.benchmark_dir().join("filename");
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
        check_filesystem(&Fxfs::new()).await;
    }

    #[fuchsia::test]
    async fn start_f2fs() {
        check_filesystem(&F2fs::new()).await;
    }

    #[fuchsia::test]
    async fn start_minfs() {
        check_filesystem(&Minfs::new()).await;
    }

    #[fuchsia::test]
    async fn start_memfs() {
        check_filesystem(&Memfs::new()).await;
    }
}
