// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::create_proxy, fidl_fuchsia_io as fio, fshost_test_fixture::TestFixtureBuilder,
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

#[fuchsia::test]
async fn blobfs_and_data_mounted() {
    let fixture = TestFixtureBuilder::new(FSHOST_COMPONENT_NAME, DATA_FILESYSTEM_FORMAT)
        .with_ramdisk()
        .format_data()
        .build()
        .await;

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
    let fixture = TestFixtureBuilder::new(FSHOST_COMPONENT_NAME, DATA_FILESYSTEM_FORMAT)
        .with_ramdisk()
        .build()
        .await;

    fixture.dir("data").describe_deprecated().await.expect("describe failed");
    fixture.check_fs_type("data", data_fs_type()).await;

    fixture.tear_down().await;
}
