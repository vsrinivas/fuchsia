// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Test cases which simulate fshost running in the configuration used in recovery builds (which,
//! among other things, sets the fvm_ramdisk flag to prevent binding of the on-disk filesystems.)

use {
    device_watcher::recursive_wait_and_open_node,
    fidl::endpoints::{create_proxy, Proxy as _},
    fidl_fuchsia_fshost as fshost,
    fidl_fuchsia_hardware_block_partition::PartitionProxy,
    fidl_fuchsia_io as fio,
    fs_management::{filesystem::Filesystem, Blobfs},
    fshost_test_fixture::TestFixtureBuilder,
    fuchsia_zircon::{self as zx},
    remote_block_device::{BlockClient, MutableBufferSlice, RemoteBlockClient},
};

const FSHOST_COMPONENT_NAME: &'static str = std::env!("FSHOST_COMPONENT_NAME");
const DATA_FILESYSTEM_FORMAT: &'static str = std::env!("DATA_FILESYSTEM_FORMAT");

fn new_builder() -> TestFixtureBuilder {
    TestFixtureBuilder::new(FSHOST_COMPONENT_NAME, DATA_FILESYSTEM_FORMAT)
}

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

// Ensure fuchsia.fshost.Admin/WipeStorage fails if we cannot identify a storage device to wipe.
#[fuchsia::test]
async fn wipe_storage_no_fvm_device() {
    let builder = new_builder();
    let fixture = builder.build().await;
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
    let builder = new_builder().with_ramdisk();
    let fixture = builder.build().await;

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
    let builder = new_builder().with_ramdisk();
    let fixture = builder.build().await;

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
    let builder = new_builder().with_ramdisk().format_data();
    let fixture = builder.build().await;

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
