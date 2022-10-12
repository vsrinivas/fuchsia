// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::test_fixture::TestFixtureBuilder, fidl::endpoints::create_proxy,
    fidl_fuchsia_fshost as fshost, fidl_fuchsia_io as fio, fuchsia_zircon as zx,
};

mod mocks;
mod test_fixture;

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

#[fuchsia::test]
async fn blobfs_and_data_mounted() {
    let fixture = TestFixtureBuilder::default().with_ramdisk().format_data().build().await;

    fixture.dir("blob").describe_deprecated().await.expect("describe failed");
    fixture.check_fs_type("blob", VFS_TYPE_BLOBFS).await;

    let (file, server) = create_proxy::<fio::NodeMarker>().unwrap();
    fixture
        .dir("data")
        .open(fio::OpenFlags::RIGHT_READABLE, 0, "foo", server)
        .expect("open failed");
    fixture.check_fs_type("data", data_fs_type()).await;
    file.describe_deprecated().await.expect("describe failed");

    fixture.tear_down().await;
}

#[fuchsia::test]
async fn data_formatted() {
    let fixture = TestFixtureBuilder::default().with_ramdisk().build().await;

    fixture.dir("data").describe_deprecated().await.expect("describe failed");
    fixture.check_fs_type("data", data_fs_type()).await;

    fixture.tear_down().await;
}

// Ensure WipeStorage is not supported in the normal mode of operation (i.e. when the `fvm_ramdisk`
// option is false). WipeStorage should only function within a recovery context.
#[fuchsia::test]
async fn wipe_storage_not_supported() {
    let fixture = TestFixtureBuilder::default().build().await;

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
