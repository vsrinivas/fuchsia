// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::create_proxy,
    fidl_fuchsia_fshost as fshost,
    fidl_fuchsia_hardware_block_partition::Guid,
    fidl_fuchsia_hardware_block_volume::{VolumeAndNodeMarker, VolumeManagerMarker},
    fidl_fuchsia_io as fio,
    fshost_test_fixture::TestFixtureBuilder,
    fshost_test_fixture::FVM_SLICE_SIZE,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_named_protocol_at_dir_root,
    fuchsia_zircon as zx,
    futures::FutureExt,
};

const FSHOST_COMPONENT_NAME: &'static str = std::env!("FSHOST_COMPONENT_NAME");
const DATA_FILESYSTEM_FORMAT: &'static str = std::env!("DATA_FILESYSTEM_FORMAT");

const VFS_TYPE_BLOBFS: u32 = 0x9e694d21;
// const VFS_TYPE_FATFS: u32 = 0xce694d21;
const VFS_TYPE_MINFS: u32 = 0x6e694d21;
const VFS_TYPE_MEMFS: u32 = 0x3e694d21;
// const VFS_TYPE_FACTORYFS: u32 = 0x1e694d21;
const VFS_TYPE_FXFS: u32 = 0x73667866;
const VFS_TYPE_F2FS: u32 = 0xfe694d21;
const BLOBFS_MAX_BYTES: u64 = 8765432;
const DATA_MAX_BYTES: u64 = 7654321;

fn data_fs_type() -> u32 {
    match DATA_FILESYSTEM_FORMAT {
        "f2fs" => VFS_TYPE_F2FS,
        "fxfs" => VFS_TYPE_FXFS,
        "minfs" => VFS_TYPE_MINFS,
        _ => panic!("invalid data filesystem format"),
    }
}

fn new_builder() -> TestFixtureBuilder {
    TestFixtureBuilder::new(FSHOST_COMPONENT_NAME, DATA_FILESYSTEM_FORMAT)
}

#[fuchsia::test]
async fn blobfs_and_data_mounted() {
    let builder = new_builder().with_ramdisk().format_data();
    let fixture = builder.build().await;

    fixture.check_fs_type("blob", VFS_TYPE_BLOBFS).await;
    fixture.check_fs_type("data", data_fs_type()).await;

    let (file, server) = create_proxy::<fio::NodeMarker>().unwrap();
    fixture
        .dir("data")
        .open(fio::OpenFlags::RIGHT_READABLE, 0, "foo", server)
        .expect("open failed");
    file.get_attr().await.expect("get_attr failed");

    fixture.tear_down().await;
}

#[fuchsia::test]
async fn data_formatted() {
    let builder = new_builder().with_ramdisk();
    let fixture = builder.build().await;

    fixture.check_fs_type("data", data_fs_type()).await;

    fixture.tear_down().await;
}

#[fuchsia::test]
async fn data_mounted_legacy_crypto_format() {
    let builder = new_builder().with_ramdisk().format_data().with_legacy_crypto_format();
    let fixture = builder.build().await;

    fixture.check_fs_type("data", data_fs_type()).await;
    let (file, server) = create_proxy::<fio::NodeMarker>().unwrap();
    fixture
        .dir("data")
        .open(fio::OpenFlags::RIGHT_READABLE, 0, "foo", server)
        .expect("open failed");
    file.get_attr().await.expect("get_attr failed");

    fixture.tear_down().await;
}

#[fuchsia::test]
async fn data_mounted_no_zxcrypt() {
    let mut builder = new_builder().with_ramdisk().format_data().without_zxcrypt();
    builder.fshost().set_no_zxcrypt();
    let fixture = builder.build().await;

    fixture.check_fs_type("data", data_fs_type()).await;
    let (file, server) = create_proxy::<fio::NodeMarker>().unwrap();
    fixture
        .dir("data")
        .open(fio::OpenFlags::RIGHT_READABLE, 0, "foo", server)
        .expect("open failed");
    file.get_attr().await.expect("get_attr failed");

    fixture.tear_down().await;
}

#[fuchsia::test]
async fn data_formatted_no_zxcrypt() {
    let mut builder = new_builder().with_ramdisk().without_zxcrypt();
    builder.fshost().set_no_zxcrypt();
    let fixture = builder.build().await;

    fixture.check_fs_type("data", data_fs_type()).await;

    fixture.tear_down().await;
}

// Ensure WipeStorage is not supported in the normal mode of operation (i.e. when the `fvm_ramdisk`
// option is false). WipeStorage should only function within a recovery context.
#[fuchsia::test]
async fn wipe_storage_not_supported() {
    let builder = new_builder();
    let fixture = builder.build().await;

    let admin =
        fixture.realm.root.connect_to_protocol_at_exposed_dir::<fshost::AdminMarker>().unwrap();

    let (_, blobfs_server) = create_proxy::<fio::DirectoryMarker>().unwrap();

    let result = admin
        .wipe_storage(blobfs_server)
        .await
        .unwrap()
        .expect_err("WipeStorage unexpectedly succeeded");
    assert_eq!(zx::Status::from_raw(result), zx::Status::NOT_SUPPORTED);

    fixture.tear_down().await;
}

#[fuchsia::test]
async fn ramdisk_blob_and_data_mounted() {
    let mut builder = new_builder().with_ramdisk().format_data().without_zxcrypt();
    builder.fshost().set_fvm_ramdisk();
    let fixture = builder.build().await;

    fixture.check_fs_type("blob", VFS_TYPE_BLOBFS).await;
    fixture.check_fs_type("data", data_fs_type()).await;
    let (file, server) = create_proxy::<fio::NodeMarker>().unwrap();
    fixture
        .dir("data")
        .open(fio::OpenFlags::RIGHT_READABLE, 0, "foo", server)
        .expect("open failed");
    file.get_attr().await.expect("get_attr failed");

    fixture.tear_down().await;
}

#[fuchsia::test]
async fn ramdisk_data_ignores_non_ramdisk() {
    let mut builder = new_builder().with_ramdisk().without_zxcrypt();
    // Fake out the ramdisk checking by providing a nonsense ramdisk prefix.
    builder.fshost().set_fvm_ramdisk().set_ramdisk_prefix("/not/the/prefix");
    let fixture = builder.build().await;

    let dev = fixture.dir("dev-topological/class/block");

    // The filesystems won't be mounted, but make sure fvm and potentially zxcrypt are bound.
    device_watcher::wait_for_device_with(&dev, |info| {
        info.topological_path.ends_with("fvm/data-p-2/block").then_some(())
    })
    .await
    .unwrap();

    if DATA_FILESYSTEM_FORMAT != "fxfs" {
        device_watcher::wait_for_device_with(&dev, |info| {
            info.topological_path
                .ends_with("fvm/data-p-2/block/zxcrypt/unsealed/block")
                .then_some(())
        })
        .await
        .unwrap();
    }

    // There isn't really a good way to tell that something is not mounted, but at this point we
    // would be pretty close to it, so a timeout of a couple seconds should safeguard against
    // potential issues.
    futures::select! {
        _ = fixture.check_fs_type("data", data_fs_type()).fuse() => {
            panic!("check_fs_type returned unexpectedly - data was mounted");
        },
        _ = fixture.check_fs_type("blob", VFS_TYPE_BLOBFS).fuse() => {
            panic!("check_fs_type returned unexpectedly - blob was mounted");
        },
        _ = fasync::Timer::new(std::time::Duration::from_secs(2)).fuse() => (),
    }

    fixture.tear_down().await;
}

async fn get_instance_guid_from_path(dir_proxy: &fio::DirectoryProxy, path: &str) -> Box<Guid> {
    let volume_proxy_data =
        connect_to_named_protocol_at_dir_root::<VolumeAndNodeMarker>(dir_proxy, path).unwrap();

    let (status, data_instance_guid) = volume_proxy_data.get_instance_guid().await.unwrap();
    assert!(zx::Status::ok(status).is_ok());
    data_instance_guid.unwrap()
}

#[fuchsia::test]
async fn partition_max_size_set() {
    let mut builder = new_builder().with_ramdisk();
    builder.fshost().set_data_max_bytes(DATA_MAX_BYTES).set_blobfs_max_bytes(BLOBFS_MAX_BYTES);
    let fixture = builder.build().await;

    fixture.check_fs_type("blob", VFS_TYPE_BLOBFS).await;
    fixture.check_fs_type("data", data_fs_type()).await;

    // get blobfs instance guid
    let mut blobfs_instance_guid = get_instance_guid_from_path(
        &fixture.dir("dev-topological"),
        "sys/platform/00:00:2d/ramctl/ramdisk-0/block/fvm/blobfs-p-1/block",
    )
    .await;

    // get data instance guid
    let mut data_instance_guid = get_instance_guid_from_path(
        &fixture.dir("dev-topological"),
        "sys/platform/00:00:2d/ramctl/ramdisk-0/block/fvm/data-p-2/block",
    )
    .await;

    let fvm_proxy = connect_to_named_protocol_at_dir_root::<VolumeManagerMarker>(
        &fixture.dir("dev-topological"),
        "sys/platform/00:00:2d/ramctl/ramdisk-0/block/fvm",
    )
    .unwrap();

    // blobfs max size check
    let (status, blobfs_slice_count) =
        fvm_proxy.get_partition_limit(blobfs_instance_guid.as_mut()).await.unwrap();
    assert!(zx::Status::ok(status).is_ok());
    assert_eq!(blobfs_slice_count, BLOBFS_MAX_BYTES / FVM_SLICE_SIZE);

    // data max size check
    let (status, data_slice_count) =
        fvm_proxy.get_partition_limit(data_instance_guid.as_mut()).await.unwrap();
    assert!(zx::Status::ok(status).is_ok());
    assert_eq!(data_slice_count, DATA_MAX_BYTES / FVM_SLICE_SIZE);
}

#[fuchsia::test]
async fn tmp_is_available() {
    let builder = new_builder();
    let fixture = builder.build().await;

    fixture.check_fs_type("tmp", VFS_TYPE_MEMFS).await;
}

#[fuchsia::test]
async fn netboot_set() {
    // Set the netboot flag
    let builder = new_builder().with_ramdisk().netboot();
    let fixture = builder.build().await;

    let dev = fixture.dir("dev-topological/class/block");

    // Filesystems will not be mounted but make sure that fvm is bound
    device_watcher::wait_for_device_with(&dev, |info| {
        info.topological_path.ends_with("fvm/data-p-2/block").then_some(())
    })
    .await
    .unwrap();

    // Use the same approach as ramdisk_data_ignores_non_ramdisk() to ensure that
    // neither blobfs nor data were mounted using a timeout
    futures::select! {
        _ = fixture.check_fs_type("data", data_fs_type()).fuse() => {
            panic!("check_fs_type returned unexpectedly - data was mounted");
        },
        _ = fixture.check_fs_type("blob", VFS_TYPE_BLOBFS).fuse() => {
            panic!("check_fs_type returned unexpectedly - blob was mounted");
        },
        _ = fasync::Timer::new(std::time::Duration::from_secs(2)).fuse() => (),
    }

    fixture.tear_down().await;
}
