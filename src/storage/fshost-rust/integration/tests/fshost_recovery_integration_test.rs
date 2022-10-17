// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Test cases which simulate fshost running in the configuration used in recovery builds (which,
//! among other things, sets the fvm_ramdisk flag to prevent binding of the on-disk filesystems.)

use {
    device_watcher::recursive_wait_and_open_node,
    either::Either,
    fidl::endpoints::{create_proxy, Proxy as _},
    fidl_fuchsia_device::ControllerMarker,
    fidl_fuchsia_fshost as fshost,
    fidl_fuchsia_fxfs::CryptMarker,
    fidl_fuchsia_hardware_block_partition::PartitionProxy,
    fidl_fuchsia_hardware_block_volume::VolumeMarker,
    fidl_fuchsia_io as fio,
    fs_management::{filesystem::Filesystem, Blobfs, Fxfs, Minfs},
    fshost_test_fixture::{create_hermetic_crypt_service, TestFixture, TestFixtureBuilder},
    fuchsia_component::client::connect_to_protocol_at_path,
    fuchsia_zircon::{self as zx, HandleBased as _},
    key_bag::{KeyBagManager, WrappingKey, AES128_KEY_SIZE},
    ramdevice_client::VmoRamdiskClientBuilder,
    remote_block_device::{BlockClient, MutableBufferSlice, RemoteBlockClient},
    std::path::Path,
    storage_isolated_driver_manager::{fvm::bind_fvm_driver, zxcrypt::unseal_insecure_zxcrypt},
};

const FSHOST_COMPONENT_NAME: &'static str = std::env!("FSHOST_COMPONENT_NAME");
const DATA_FILESYSTEM_FORMAT: &'static str = std::env!("DATA_FILESYSTEM_FORMAT");

// Blob containing 8192 bytes of 0xFF ("oneblock").
const TEST_BLOB_LEN: u64 = 8192;
const TEST_BLOB_DATA: [u8; TEST_BLOB_LEN as usize] = [0xFF; TEST_BLOB_LEN as usize];
const TEST_BLOB_NAME: &'static str =
    "68d131bc271f9c192d4f6dcd8fe61bef90004856da19d0f2f514a7f4098b0737";

async fn write_test_blob(directory: &fio::DirectoryProxy) {
    let test_blob = fuchsia_fs::directory::open_file(
        directory,
        TEST_BLOB_NAME,
        fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
    )
    .await
    .unwrap();
    test_blob.resize(TEST_BLOB_LEN).await.unwrap().expect("Resize failed");
    let bytes_written = test_blob.write(&TEST_BLOB_DATA).await.unwrap().expect("Write failed");
    assert_eq!(bytes_written, TEST_BLOB_LEN);
}

fn generate_insecure_key(name: &[u8]) -> WrappingKey {
    let mut bytes = [0u8; AES128_KEY_SIZE];
    bytes[..name.len()].copy_from_slice(&name);
    WrappingKey::Aes128(bytes)
}

async fn write_data_file_common(fixture: TestFixture, expected_volume_size: u64) {
    const PAYLOAD: &[u8] = b"top secret stuff";

    {
        let admin =
            fixture.realm.root.connect_to_protocol_at_exposed_dir::<fshost::AdminMarker>().unwrap();
        let vmo = zx::Vmo::create(1024).unwrap();
        vmo.write(PAYLOAD, 0).unwrap();
        vmo.set_content_size(&(PAYLOAD.len() as u64)).unwrap();
        admin
            .write_data_file("inconspicuous/secret.txt", vmo)
            .await
            .expect("FIDL failed")
            .expect("write_data_file failed");
    }

    let vmo = fixture.ramdisk_vmo().unwrap().duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap();
    fixture.tear_down().await;

    // Manually remount the filesystem so we can verify the existence of the installed file.
    let ramdisk = VmoRamdiskClientBuilder::new(vmo).block_size(512).build().unwrap();
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
    // Matches the configured value in //src/storage/fshost/fshost.gni in
    // default_integration_test_options, which fshost will use when reformatting the volume.
    const EXPECTED_VOLUME_SIZE: u64 = 117440512;
    write_data_file_common(fixture, EXPECTED_VOLUME_SIZE).await;
}

#[fuchsia::test]
async fn write_data_file_unformatted_small_disk() {
    let fixture = TestFixtureBuilder::new(FSHOST_COMPONENT_NAME, DATA_FILESYSTEM_FORMAT)
        .with_sized_ramdisk(25165824)
        .build()
        .await;
    // The expected size is everything left in the FVM block device.
    const EXPECTED_VOLUME_SIZE: u64 = 23691264;
    write_data_file_common(fixture, EXPECTED_VOLUME_SIZE).await;
}

#[fuchsia::test]
async fn write_data_file_formatted() {
    let fixture = TestFixtureBuilder::new(FSHOST_COMPONENT_NAME, DATA_FILESYSTEM_FORMAT)
        .format_data()
        .with_ramdisk()
        .build()
        .await;
    // Matches the value we configured the volume to in TestFixture.
    const EXPECTED_VOLUME_SIZE: u64 = 16 * 1024 * 1024;
    write_data_file_common(fixture, EXPECTED_VOLUME_SIZE).await;
}

// Ensure fuchsia.fshost.Admin/WipeStorage fails if we cannot identify a storage device to wipe.
#[fuchsia::test]
async fn wipe_storage_no_fvm_device() {
    let fixture =
        TestFixtureBuilder::new(FSHOST_COMPONENT_NAME, DATA_FILESYSTEM_FORMAT).build().await;
    let admin =
        fixture.realm.root.connect_to_protocol_at_exposed_dir::<fshost::AdminMarker>().unwrap();
    let (_, blobfs_server) = create_proxy::<fio::DirectoryMarker>().unwrap();
    let result = admin
        .wipe_storage(blobfs_server)
        .await
        .expect("FIDL call to WipeStorage failed")
        .expect_err("WipeStorage unexpectedly succeeded");
    assert_eq!(zx::Status::from_raw(result), zx::Status::NOT_FOUND);
    fixture.tear_down().await;
}

// Demonstrate high level usage of the fuchsia.fshost.Admin/WipeStorage method.
#[fuchsia::test]
async fn wipe_storage_write_blob() {
    let fixture = TestFixtureBuilder::new(FSHOST_COMPONENT_NAME, DATA_FILESYSTEM_FORMAT)
        .with_ramdisk()
        .build()
        .await;

    // Invoke WipeStorage, which will unbind the FVM, reprovision it, and format/mount Blobfs.
    let admin =
        fixture.realm.root.connect_to_protocol_at_exposed_dir::<fshost::AdminMarker>().unwrap();
    let (blobfs_root, blobfs_server) = create_proxy::<fio::DirectoryMarker>().unwrap();
    admin.wipe_storage(blobfs_server).await.unwrap().expect("WipeStorage unexpectedly failed");

    // Ensure that we can write a blob into the new Blobfs instance.
    write_test_blob(&blobfs_root).await;

    fixture.tear_down().await;
}

// Verify that all existing blobs are purged after running fuchsia.fshost.Admin/WipeStorage.
#[fuchsia::test]
async fn wipe_storage_blobfs_formatted() {
    let fixture = TestFixtureBuilder::new(FSHOST_COMPONENT_NAME, DATA_FILESYSTEM_FORMAT)
        .with_ramdisk()
        .build()
        .await;

    // Mount Blobfs and write a blob.
    {
        let blobfs_dev_path =
            format!("{}/fvm/blobfs-p-1/block", fixture.ramdisk.as_ref().unwrap().get_path());
        let blobfs_dev_node =
            recursive_wait_and_open_node(&fixture.dir("dev-topological"), &blobfs_dev_path)
                .await
                .unwrap();
        let blobfs = Filesystem::from_node(
            blobfs_dev_node,
            Blobfs {
                verbose: false,
                readonly: false,
                blob_deprecated_padded_format: false, // default is false for integration tests
                ..Default::default()
            },
        )
        .serve()
        .await
        .unwrap();

        let blobfs_root = &blobfs.root();
        write_test_blob(blobfs_root).await;
        assert!(fuchsia_fs::directory::dir_contains(blobfs_root, TEST_BLOB_NAME).await.unwrap());
    }

    // Invoke the WipeStorage API.
    let admin =
        fixture.realm.root.connect_to_protocol_at_exposed_dir::<fshost::AdminMarker>().unwrap();
    let (blobfs_root, blobfs_server) = create_proxy::<fio::DirectoryMarker>().unwrap();
    admin.wipe_storage(blobfs_server).await.unwrap().expect("WipeStorage unexpectedly failed");

    // Verify there are no more blobs.
    assert!(fuchsia_fs::directory::readdir(&blobfs_root).await.unwrap().is_empty());

    fixture.tear_down().await;
}

// Verify that the data partition is wiped and remains unformatted.
#[fuchsia::test]
async fn wipe_storage_data_unformatted() {
    const BUFF_LEN: usize = 512;
    let fixture = TestFixtureBuilder::new(FSHOST_COMPONENT_NAME, DATA_FILESYSTEM_FORMAT)
        .with_ramdisk()
        .format_data()
        .build()
        .await;

    let data_dev_path =
        format!("{}/fvm/data-p-2/block", fixture.ramdisk.as_ref().unwrap().get_path());

    let orig_instance_guid;
    {
        let data_dev_node =
            recursive_wait_and_open_node(&fixture.dir("dev-topological"), &data_dev_path)
                .await
                .unwrap();
        let data_partition = PartitionProxy::from_channel(data_dev_node.into_channel().unwrap());
        let (status, guid) = data_partition.get_instance_guid().await.unwrap();
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
        orig_instance_guid = guid.unwrap();

        let block_client =
            RemoteBlockClient::new(data_partition.into_channel().unwrap().into()).await.unwrap();
        let mut buff: [u8; BUFF_LEN] = [0; BUFF_LEN];
        block_client.read_at(MutableBufferSlice::Memory(&mut buff), 0).await.unwrap();
        // The data partition should have been formatted so there should be some non-zero bytes.
        assert_ne!(buff, [0; BUFF_LEN]);
    }

    // TODO(fxbug.dev/112142): Due to a race between the block watcher and WipeStorage, we have to
    // wait for zxcrypt to be unsealed to ensure all child drivers of the FVM device are bound.
    if DATA_FILESYSTEM_FORMAT != "fxfs" {
        let zxcrypt_dev_path = format!("{}/zxcrypt/unsealed/block", &data_dev_path);
        recursive_wait_and_open_node(&fixture.dir("dev-topological"), &zxcrypt_dev_path)
            .await
            .unwrap();
    }

    // Invoke WipeStorage.
    let admin =
        fixture.realm.root.connect_to_protocol_at_exposed_dir::<fshost::AdminMarker>().unwrap();
    let (_, blobfs_server) = create_proxy::<fio::DirectoryMarker>().unwrap();
    admin.wipe_storage(blobfs_server).await.unwrap().expect("WipeStorage unexpectedly failed");

    // Ensure the data partition was assigned a new instance GUID.
    let data_dev_node =
        recursive_wait_and_open_node(&fixture.dir("dev-topological"), &data_dev_path)
            .await
            .unwrap();
    let data_partition = PartitionProxy::from_channel(data_dev_node.into_channel().unwrap());
    let (status, guid) = data_partition.get_instance_guid().await.unwrap();
    assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
    assert_ne!(guid.unwrap(), orig_instance_guid);

    // The data partition should remain unformatted, so the first few bytes should be all zero now.
    let block_client =
        RemoteBlockClient::new(data_partition.into_channel().unwrap().into()).await.unwrap();
    let mut buff: [u8; BUFF_LEN] = [0; BUFF_LEN];
    block_client.read_at(MutableBufferSlice::Memory(&mut buff), 0).await.unwrap();
    assert_eq!(buff, [0; BUFF_LEN]);

    fixture.tear_down().await;
}
