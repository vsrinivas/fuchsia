// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::create_proxy, fidl_fuchsia_fshost as fshost, fidl_fuchsia_io as fio,
    fshost_test_fixture::TestFixtureBuilder, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::FutureExt,
};

const FSHOST_COMPONENT_NAME: &'static str = std::env!("FSHOST_COMPONENT_NAME");
const DATA_FILESYSTEM_FORMAT: &'static str = std::env!("DATA_FILESYSTEM_FORMAT");

const VFS_TYPE_BLOBFS: u32 = 0x9e694d21;
// const VFS_TYPE_FATFS: u32 = 0xce694d21;
const VFS_TYPE_MINFS: u32 = 0x6e694d21;
// const VFS_TYPE_MEMFS: u32 = 0x3e694d21;
// const VFS_TYPE_FACTORYFS: u32 = 0x1e694d21;
const VFS_TYPE_FXFS: u32 = 0x73667866;
const VFS_TYPE_F2FS: u32 = 0xfe694d21;

fn data_fs_type() -> u32 {
    match DATA_FILESYSTEM_FORMAT {
        "f2fs" => VFS_TYPE_F2FS,
        "fxfs" => VFS_TYPE_FXFS,
        "minfs" => VFS_TYPE_MINFS,
        _ => panic!("invalid data filesystem format"),
    }
}

fn new_fixture() -> TestFixtureBuilder {
    TestFixtureBuilder::new(FSHOST_COMPONENT_NAME, DATA_FILESYSTEM_FORMAT)
}

#[fuchsia::test]
async fn blobfs_and_data_mounted() {
    let fixture = new_fixture().with_ramdisk().format_data().build().await;

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
    let fixture = new_fixture().with_ramdisk().build().await;

    fixture.check_fs_type("data", data_fs_type()).await;

    fixture.tear_down().await;
}

#[fuchsia::test]
async fn data_mounted_no_zxcrypt() {
    let fixture = new_fixture().with_ramdisk().format_data().no_zxcrypt().build().await;

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
    let fixture = new_fixture().with_ramdisk().no_zxcrypt().build().await;

    fixture.check_fs_type("data", data_fs_type()).await;

    fixture.tear_down().await;
}

// Ensure WipeStorage is not supported in the normal mode of operation (i.e. when the `fvm_ramdisk`
// option is false). WipeStorage should only function within a recovery context.
#[fuchsia::test]
async fn wipe_storage_not_supported() {
    let fixture =
        TestFixtureBuilder::new(FSHOST_COMPONENT_NAME, DATA_FILESYSTEM_FORMAT).build().await;

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
    let fixture = new_fixture().with_ramdisk().format_data().fvm_ramdisk().build().await;

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
    // Fake out the ramdisk checking by providing a nonsense ramdisk prefix.
    let fixture =
        new_fixture().with_ramdisk().fvm_ramdisk().ramdisk_prefix("/not/the/prefix").build().await;

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
