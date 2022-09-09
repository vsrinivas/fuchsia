// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests the property that the FuchsiaBootResolver successfully
/// resolves components that are encoded in a meta.far. This test is fully
/// hermetic.
use {
    component_manager_lib::{
        bootfs::BootfsSvc, builtin::fuchsia_boot_resolver::FuchsiaBootResolver,
    },
    fidl_fuchsia_component_resolution as fresolution, fidl_fuchsia_io as fio,
    fuchsia_async as fasync, fuchsia_fs,
    std::path::Path,
};

// macros
use vfs::assert_read_dirents;

use vfs::directory::test_utils::DirentsSameInodeBuilder;

const ZBI_PATH: &str = "/pkg/data/tests/uncompressed_bootfs";
const HELLO_WORLD_URL: &str = "fuchsia-boot:///hello_world#meta/hello_world.cm";
#[fasync::run(2, test)]
async fn package_resolution() {
    let bootfs_image = fuchsia_fs::file::open_in_namespace(
        ZBI_PATH,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
    )
    .unwrap();

    let vmo = bootfs_image
        .get_backing_memory(fio::VmoFlags::READ | fio::VmoFlags::EXECUTE)
        .await
        .unwrap()
        .unwrap();

    let bootfs_svc = BootfsSvc::new_for_test(vmo).unwrap();

    bootfs_svc.ingest_bootfs_vmo_for_test().unwrap().create_and_bind_vfs().unwrap();

    let boot_resolver = FuchsiaBootResolver::new("/boot").await.unwrap().unwrap();

    let fresolution::Component { url, package, .. } =
        boot_resolver.resolve_async(HELLO_WORLD_URL).await.unwrap();

    assert_eq!(url.unwrap(), HELLO_WORLD_URL);

    let package_url = &package.as_ref().unwrap().url.as_ref().unwrap();
    assert_eq!(*package_url.to_owned(), "fuchsia-boot:///hello_world".to_string());

    let dir_proxy = package.unwrap().directory.unwrap().into_proxy().unwrap();

    let mut expected = DirentsSameInodeBuilder::new(fio::INO_UNKNOWN);
    expected
        .add(fio::DirentType::Directory, b".")
        .add(fio::DirentType::Directory, b"bin")
        .add(fio::DirentType::Directory, b"lib")
        .add(fio::DirentType::Directory, b"meta");

    assert_read_dirents!(dir_proxy, 1000, expected.into_vec());

    let mut expected_bin = DirentsSameInodeBuilder::new(fio::INO_UNKNOWN);
    expected_bin
        .add(fio::DirentType::Directory, b".")
        .add(fio::DirentType::File, b"hello_world_v1");
    assert_read_dirents!(
        fuchsia_fs::open_directory(&dir_proxy, Path::new("bin"), fio::OpenFlags::RIGHT_READABLE)
            .unwrap(),
        1000,
        expected_bin.into_vec()
    );

    let mut expected_meta = DirentsSameInodeBuilder::new(fio::INO_UNKNOWN);
    expected_meta
        .add(fio::DirentType::Directory, b".")
        .add(fio::DirentType::File, b"contents")
        .add(fio::DirentType::Directory, b"fuchsia.abi")
        .add(fio::DirentType::File, b"hello_world.cm")
        .add(fio::DirentType::File, b"package");

    assert_read_dirents!(
        fuchsia_fs::open_directory(&dir_proxy, Path::new("meta"), fio::OpenFlags::RIGHT_READABLE)
            .unwrap(),
        1000,
        expected_meta.into_vec()
    );
}
