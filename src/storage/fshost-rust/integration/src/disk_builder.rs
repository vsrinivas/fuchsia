// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::create_hermetic_crypt_service,
    device_watcher::recursive_wait_and_open_node,
    fidl::endpoints::{create_proxy, Proxy},
    fidl_fuchsia_fxfs::CryptMarker,
    fidl_fuchsia_io as fio,
    fs_management::{Blobfs, Fxfs, BLOBFS_TYPE_GUID, DATA_TYPE_GUID},
    fuchsia_zircon::{self as zx, HandleBased},
    key_bag::Aes256Key,
    ramdevice_client::VmoRamdiskClientBuilder,
    std::{io::Write, path::Path},
    storage_isolated_driver_manager::{
        fvm::{create_fvm_volume, set_up_fvm},
        zxcrypt,
    },
    uuid::Uuid,
};

pub const DEFAULT_DISK_SIZE: u64 = 234881024;

// We use a static key-bag so that the crypt instance can be shared across test executions safely.
// These keys match the DATA_KEY and METADATA_KEY respectively, when wrapped with the "zxcrypt"
// static key used by fshost.
// Note this isn't used in the legacy crypto format.
const KEY_BAG_CONTENTS: &'static str = "\
{
    \"version\":1,
    \"keys\": {
        \"0\":{
            \"Aes128GcmSivWrapped\": [
                \"7a7c6a718cfde7078f6edec5\",
                \"7cc31b765c74db3191e269d2666267022639e758fe3370e8f36c166d888586454fd4de8aeb47aadd81c531b0a0a66f27\"
            ]
        },
        \"1\":{
            \"Aes128GcmSivWrapped\": [
                \"b7d7f459cbee4cc536cc4324\",
                \"9f6a5d894f526b61c5c091e5e02a7ff94d18e6ad36a0aa439c86081b726eca79e6b60bd86ee5d86a20b3df98f5265a99\"
            ]
        }
    }
}";

const DATA_KEY: Aes256Key = Aes256Key::create([
    0xcf, 0x9e, 0x45, 0x2a, 0x22, 0xa5, 0x70, 0x31, 0x33, 0x3b, 0x4d, 0x6b, 0x6f, 0x78, 0x58, 0x29,
    0x04, 0x79, 0xc7, 0xd6, 0xa9, 0x4b, 0xce, 0x82, 0x04, 0x56, 0x5e, 0x82, 0xfc, 0xe7, 0x37, 0xa8,
]);

const METADATA_KEY: Aes256Key = Aes256Key::create([
    0x0f, 0x4d, 0xca, 0x6b, 0x35, 0x0e, 0x85, 0x6a, 0xb3, 0x8c, 0xdd, 0xe9, 0xda, 0x0e, 0xc8, 0x22,
    0x8e, 0xea, 0xd8, 0x05, 0xc4, 0xc9, 0x0b, 0xa8, 0xd8, 0x85, 0x87, 0x50, 0x75, 0x40, 0x1c, 0x4c,
]);

// Matches the hard-coded value used by fshost when use_native_fxfs_crypto is false.
const LEGACY_DATA_KEY: Aes256Key = Aes256Key::create([
    0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11,
    0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
]);

// Matches the hard-coded value used by fshost when use_native_fxfs_crypto is false.
const LEGACY_METADATA_KEY: Aes256Key = Aes256Key::create([
    0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8, 0xf7, 0xf6, 0xf5, 0xf4, 0xf3, 0xf2, 0xf1, 0xf0,
    0xef, 0xee, 0xed, 0xec, 0xeb, 0xea, 0xe9, 0xe8, 0xe7, 0xe6, 0xe5, 0xe4, 0xe3, 0xe2, 0xe1, 0xe0,
]);

pub struct DiskBuilder {
    size: u64,
    format: Option<&'static str>,
    legacy_crypto_format: bool,
    zxcrypt: bool,
}

impl DiskBuilder {
    pub fn new() -> DiskBuilder {
        DiskBuilder {
            size: DEFAULT_DISK_SIZE,
            format: None,
            legacy_crypto_format: false,
            zxcrypt: true,
        }
    }

    pub fn size(&mut self, size: u64) -> &mut Self {
        self.size = size;
        self
    }

    pub fn format_data(&mut self, format: &'static str) -> &mut Self {
        self.format = Some(format);
        self
    }

    pub fn legacy_crypto_format(&mut self) -> &mut Self {
        self.legacy_crypto_format = true;
        self
    }

    pub fn without_zxcrypt(&mut self) -> &mut Self {
        self.zxcrypt = false;
        self
    }

    pub(crate) async fn build(&self) -> zx::Vmo {
        let (dev, server) = create_proxy::<fio::DirectoryMarker>().unwrap();
        fdio::open(
            "/dev",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            server.into_channel(),
        )
        .unwrap();

        recursive_wait_and_open_node(&dev, "sys/platform/00:00:2d/ramctl")
            .await
            .expect("recursive_wait_and_open_node failed");

        let vmo = zx::Vmo::create(self.size).unwrap();
        let vmo_dup = vmo.duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap();
        let ramdisk = VmoRamdiskClientBuilder::new(vmo).block_size(512).build().unwrap();
        let ramdisk_path = ramdisk.get_path();

        let volume_manager_proxy =
            set_up_fvm(Path::new(ramdisk_path), 32 * 1024).await.expect("set_up_fvm failed");

        create_fvm_volume(
            &volume_manager_proxy,
            "blobfs",
            &BLOBFS_TYPE_GUID,
            Uuid::new_v4().as_bytes(),
            None,
            0,
        )
        .await
        .expect("create_fvm_volume failed");
        let blobfs_path = format!("{}/fvm/blobfs-p-1/block", ramdisk_path);
        recursive_wait_and_open_node(&dev, &blobfs_path.strip_prefix("/dev/").unwrap())
            .await
            .expect("recursive_wait_and_open_node failed");
        let mut blobfs = Blobfs::new(&blobfs_path).expect("new failed");
        blobfs.format().await.expect("format failed");

        create_fvm_volume(
            &volume_manager_proxy,
            "data",
            &DATA_TYPE_GUID,
            Uuid::new_v4().as_bytes(),
            Some(16 * 1024 * 1024),
            0,
        )
        .await
        .expect("create_fvm_volume failed");

        if let Some(format) = self.format {
            match format {
                "fxfs" => self.init_data_fxfs(ramdisk_path, &dev).await,
                "minfs" => self.init_data_minfs(ramdisk_path, &dev).await,
                _ => panic!("unsupported data filesystem format type"),
            }
        }

        ramdisk.destroy().expect("destroy failed");

        vmo_dup
    }

    async fn init_data_minfs(&self, ramdisk_path: &str, dev: &fio::DirectoryProxy) {
        let data_path = format!("{}/fvm/data-p-2/block", ramdisk_path);
        let mut data_device =
            recursive_wait_and_open_node(&dev, &data_path.strip_prefix("/dev/").unwrap())
                .await
                .expect("recursive_wait_and_open_node failed");
        if self.zxcrypt {
            let zxcrypt_path = zxcrypt::set_up_insecure_zxcrypt(Path::new(&data_path))
                .await
                .expect("failed to set up zxcrypt");
            let zxcrypt_path = zxcrypt_path.as_os_str().to_str().unwrap();
            data_device =
                recursive_wait_and_open_node(dev, zxcrypt_path.strip_prefix("/dev/").unwrap())
                    .await
                    .expect("recursive_wait_and_open_node failed");
        }
        let mut minfs = fs_management::Minfs::from_channel(
            data_device.into_channel().unwrap().into_zx_channel(),
        )
        .expect("from_channel failed");
        minfs.format().await.expect("format failed");
        let fs = minfs.serve().await.expect("serve_single_volume failed");
        // Create a file called "foo" that tests can test for presence.
        let (file, server) = create_proxy::<fio::NodeMarker>().unwrap();
        fs.root()
            .open(
                fio::OpenFlags::RIGHT_READABLE
                    | fio::OpenFlags::RIGHT_WRITABLE
                    | fio::OpenFlags::CREATE,
                0,
                "foo",
                server,
            )
            .expect("open failed");
        // We must solicit a response since otherwise shutdown below could race and creation of
        // the file could get dropped.
        let _: Vec<_> = file.query().await.expect("query failed");
        fs.shutdown().await.expect("shutdown failed");
    }

    async fn init_data_fxfs(&self, ramdisk_path: &str, dev: &fio::DirectoryProxy) {
        let (data_key, metadata_key) = if self.legacy_crypto_format {
            (LEGACY_DATA_KEY, LEGACY_METADATA_KEY)
        } else {
            (DATA_KEY, METADATA_KEY)
        };
        let crypt_realm = create_hermetic_crypt_service(data_key, metadata_key).await;
        let data_path = format!("{}/fvm/data-p-2/block", ramdisk_path);
        let data_device =
            recursive_wait_and_open_node(dev, data_path.strip_prefix("/dev/").unwrap())
                .await
                .expect("recursive_wait_and_open_node failed");
        let mut fxfs = Fxfs::from_channel(data_device.into_channel().unwrap().into_zx_channel())
            .expect("from_channel failed");
        fxfs.format().await.expect("format failed");
        let mut fs = fxfs.serve_multi_volume().await.expect("serve_multi_volume failed");
        let vol = if self.legacy_crypto_format {
            let crypt_service = Some(
                crypt_realm
                    .root
                    .connect_to_protocol_at_exposed_dir::<CryptMarker>()
                    .expect("Unable to connect to Crypt service")
                    .into_channel()
                    .unwrap()
                    .into_zx_channel()
                    .into(),
            );
            fs.create_volume("default", crypt_service).await.expect("create_volume failed")
        } else {
            let vol = fs.create_volume("unencrypted", None).await.expect("create_volume failed");
            vol.bind_to_path("/unencrypted_volume").unwrap();
            // Initialize the key-bag with the static keys.
            std::fs::create_dir("/unencrypted_volume/keys").expect("create_dir failed");
            let mut file = std::fs::File::create("/unencrypted_volume/keys/fxfs-data")
                .expect("create file failed");
            file.write_all(KEY_BAG_CONTENTS.as_bytes()).expect("write file failed");

            let crypt_service = Some(
                crypt_realm
                    .root
                    .connect_to_protocol_at_exposed_dir::<CryptMarker>()
                    .expect("Unable to connect to Crypt service")
                    .into_channel()
                    .unwrap()
                    .into_zx_channel()
                    .into(),
            );
            fs.create_volume("data", crypt_service).await.expect("create_volume failed")
        };
        // Create a file called "foo" that tests can test for presence.
        let (file, server) = create_proxy::<fio::NodeMarker>().unwrap();
        vol.root()
            .open(
                fio::OpenFlags::RIGHT_READABLE
                    | fio::OpenFlags::RIGHT_WRITABLE
                    | fio::OpenFlags::CREATE,
                0,
                "foo",
                server,
            )
            .expect("open failed");
        // We must solicit a response since otherwise shutdown below could race and creation of
        // the file could get dropped.
        let _: Vec<_> = file.query().await.expect("query failed");
        fs.shutdown().await.expect("shutdown failed");
    }
}
