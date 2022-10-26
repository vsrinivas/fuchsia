// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Test cases which simulate fshost running in the configuration used in recovery builds (which,
//! among other things, sets the fvm_ramdisk flag to prevent binding of the on-disk filesystems.)

use {
    device_watcher::recursive_wait_and_open_node,
    either::Either,
    fidl::endpoints::Proxy as _,
    fidl_fuchsia_device::ControllerMarker,
    fidl_fuchsia_fshost as fshost,
    fidl_fuchsia_fxfs::CryptMarker,
    fidl_fuchsia_hardware_block_volume::VolumeMarker,
    fidl_fuchsia_io as fio,
    fs_management::{filesystem::Filesystem, Fxfs, Minfs},
    fshost::AdminProxy,
    fshost_test_fixture::{create_hermetic_crypt_service, TestFixtureBuilder},
    fuchsia_component::client::connect_to_protocol_at_path,
    fuchsia_zircon::{self as zx, HandleBased as _},
    key_bag::{KeyBagManager, WrappingKey, AES128_KEY_SIZE},
    ramdevice_client::{RamdiskClient, VmoRamdiskClientBuilder},
    std::path::Path,
    storage_isolated_driver_manager::{fvm::bind_fvm_driver, zxcrypt::unseal_insecure_zxcrypt},
};

const FSHOST_COMPONENT_NAME: &'static str = std::env!("FSHOST_COMPONENT_NAME");
const DATA_FILESYSTEM_FORMAT: &'static str = std::env!("DATA_FILESYSTEM_FORMAT");
const PAYLOAD: &[u8] = b"top secret stuff";

fn generate_insecure_key(name: &[u8]) -> WrappingKey {
    let mut bytes = [0u8; AES128_KEY_SIZE];
    bytes[..name.len()].copy_from_slice(&name);
    WrappingKey::Aes128(bytes)
}

async fn call_write_data_file(admin: &AdminProxy) {
    let vmo = zx::Vmo::create(1024).unwrap();
    vmo.write(PAYLOAD, 0).unwrap();
    vmo.set_content_size(&(PAYLOAD.len() as u64)).unwrap();
    admin
        .write_data_file("inconspicuous/secret.txt", vmo)
        .await
        .expect("FIDL failed")
        .expect("write_data_file failed");
}

async fn verify_file_contents(ramdisk: RamdiskClient, expected_volume_size: u64, formatted: bool) {
    // Manually remount the filesystem so we can verify the existence of the installed file.
    let ramdisk_path = ramdisk.get_path();
    let controller = connect_to_protocol_at_path::<ControllerMarker>(ramdisk_path).unwrap();
    let _fvm = bind_fvm_driver(&controller).await.expect("bind fvm failed");
    let data_path = format!("{}/fvm/data-p-2/block", ramdisk_path);
    let dev = fuchsia_fs::directory::open_in_namespace("/dev", fio::OpenFlags::RIGHT_READABLE)
        .expect("open /dev failed");
    let _ = recursive_wait_and_open_node(&dev, data_path.strip_prefix("/dev/").unwrap())
        .await
        .expect("recursive_wait_and_open_node failed");

    let crypt_realm;
    let mut fs;
    let _vol;
    let data_root;
    if DATA_FILESYSTEM_FORMAT == "fxfs" {
        fs = Either::Left(
            Filesystem::from_path(&data_path, Fxfs { readonly: true, ..Default::default() })
                .expect("from_path failed")
                .serve_multi_volume()
                .await
                .expect("serve_multi_volume failed"),
        );
        let fs = fs.as_mut().left().unwrap();
        // We have to reinitialize a new crypt service, since fshost picked new keys for the volume
        // if the volume was nonexistent when WriteDataFile was called.
        _vol = {
            let vol = fs.open_volume("unencrypted", None).await.expect("open_volume failed");
            vol.bind_to_path("/unencrypted_volume").unwrap();
            let keybag = KeyBagManager::open(Path::new("/unencrypted_volume/keys/fxfs-data"))
                .expect("open keybag failed");
            let unwrap_key = generate_insecure_key(b"zxcrypt");
            let data_unwrapped = keybag.unwrap_key(0, &unwrap_key).expect("unwrap key failed");
            let metadata_unwrapped = keybag.unwrap_key(1, &unwrap_key).expect("unwrap key failed");

            crypt_realm = create_hermetic_crypt_service(data_unwrapped, metadata_unwrapped).await;
            let crypt_service = Some(
                crypt_realm
                    .root
                    .connect_to_protocol_at_exposed_dir::<CryptMarker>()
                    .unwrap()
                    .into_channel()
                    .unwrap()
                    .into_zx_channel()
                    .into(),
            );
            fs.open_volume("data", crypt_service).await.expect("open_volume failed")
        };
        data_root = _vol.root();
    } else {
        let nested_path =
            unseal_insecure_zxcrypt(Path::new(&data_path)).await.expect("failed to set up zxcrypt");
        let nested_path = nested_path.as_os_str().to_str().unwrap();
        let node = recursive_wait_and_open_node(&dev, nested_path.strip_prefix("/dev/").unwrap())
            .await
            .expect("recursive_wait_and_open_node failed");
        fs = Either::Right(
            Filesystem::from_node(node, Minfs { readonly: true, ..Default::default() })
                .serve()
                .await
                .expect("serve failed"),
        );
        data_root = fs.as_ref().right().unwrap().root();
    }
    let file = fuchsia_fs::directory::open_file(
        data_root,
        "inconspicuous/secret.txt",
        fio::OpenFlags::RIGHT_READABLE,
    )
    .await
    .expect("open_file failed");
    let contents = fuchsia_fs::file::read(&file).await.expect("read failed");
    assert_eq!(&contents[..], PAYLOAD);

    if formatted {
        let file =
            fuchsia_fs::directory::open_file(data_root, "foo", fio::OpenFlags::RIGHT_READABLE)
                .await
                .expect("open_file failed");
        file.get_attr().await.expect("get_attr failed");
    }

    if DATA_FILESYSTEM_FORMAT != "minfs" {
        let volume = connect_to_protocol_at_path::<VolumeMarker>(&data_path).unwrap();
        let (status, manager_info, info) = volume.get_volume_info().await.expect("get info failed");
        assert_eq!(status, zx::Status::OK.into_raw());
        let manager_info = manager_info.unwrap();
        let info = info.unwrap();
        assert_eq!(expected_volume_size, info.partition_slice_count * manager_info.slice_size);
    }
}

#[fuchsia::test]
async fn write_data_file_unformatted() {
    let fixture = TestFixtureBuilder::new(FSHOST_COMPONENT_NAME, DATA_FILESYSTEM_FORMAT)
        .with_ramdisk()
        .build()
        .await;
    let admin =
        fixture.realm.root.connect_to_protocol_at_exposed_dir::<fshost::AdminMarker>().unwrap();
    call_write_data_file(&admin).await;
    let vmo = fixture.ramdisk_vmo().unwrap().duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap();
    fixture.tear_down().await;
    let ramdisk = VmoRamdiskClientBuilder::new(vmo).block_size(512).build().unwrap();
    // Matches the configured value in //src/storage/fshost/fshost.gni in
    // default_integration_test_options, which fshost will use when reformatting the volume.
    const EXPECTED_VOLUME_SIZE: u64 = 117440512;
    verify_file_contents(ramdisk, EXPECTED_VOLUME_SIZE, false).await;
}

#[fuchsia::test]
async fn write_data_file_unformatted_small_disk() {
    let fixture = TestFixtureBuilder::new(FSHOST_COMPONENT_NAME, DATA_FILESYSTEM_FORMAT)
        .with_sized_ramdisk(25165824)
        .build()
        .await;
    let admin =
        fixture.realm.root.connect_to_protocol_at_exposed_dir::<fshost::AdminMarker>().unwrap();
    call_write_data_file(&admin).await;
    let vmo = fixture.ramdisk_vmo().unwrap().duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap();
    fixture.tear_down().await;
    let ramdisk = VmoRamdiskClientBuilder::new(vmo).block_size(512).build().unwrap();
    // The expected size is everything left in the FVM block device.
    const EXPECTED_VOLUME_SIZE: u64 = 23691264;
    verify_file_contents(ramdisk, EXPECTED_VOLUME_SIZE, false).await;
}

#[fuchsia::test]
async fn write_data_file_formatted() {
    let fixture = TestFixtureBuilder::new(FSHOST_COMPONENT_NAME, DATA_FILESYSTEM_FORMAT)
        .format_data()
        .with_ramdisk()
        .build()
        .await;
    let admin =
        fixture.realm.root.connect_to_protocol_at_exposed_dir::<fshost::AdminMarker>().unwrap();
    call_write_data_file(&admin).await;
    let vmo = fixture.ramdisk_vmo().unwrap().duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap();
    fixture.tear_down().await;
    let ramdisk = VmoRamdiskClientBuilder::new(vmo).block_size(512).build().unwrap();
    // Matches the value we configured the volume to in TestFixture.
    const EXPECTED_VOLUME_SIZE: u64 = 16 * 1024 * 1024;
    verify_file_contents(ramdisk, EXPECTED_VOLUME_SIZE, true).await;
}
