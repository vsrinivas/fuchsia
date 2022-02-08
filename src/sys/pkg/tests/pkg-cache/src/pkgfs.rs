// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::TestEnv,
    blobfs_ramdisk::BlobfsRamdisk,
    fuchsia_pkg_testing::{PackageBuilder, SystemImageBuilder},
    pkgfs_ramdisk::PkgfsRamdisk,
};

#[fuchsia_async::run_singlethreaded(test)]
async fn expose_pkgfs_ctl_validation_missing_file() {
    let blobfs = BlobfsRamdisk::start().unwrap();
    let base_package_with_missing_blob = PackageBuilder::new("has-missing-blob")
        .add_resource_at("missing-blob", b"missing-blob-contents".as_slice())
        .build()
        .await
        .unwrap();
    let system_image_package =
        SystemImageBuilder::new().static_packages(&[&base_package_with_missing_blob]).build().await;
    base_package_with_missing_blob.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let mut missing_blob = None;
    for blob_info in base_package_with_missing_blob.content_blob_files() {
        let hash = blob_info.merkle.to_string();
        let () = blobfs
            .root_dir_proxy()
            .unwrap()
            .unlink(&hash, fidl_fuchsia_io::UnlinkOptions::EMPTY)
            .await
            .unwrap()
            .unwrap();
        assert!(missing_blob.is_none());
        missing_blob = Some(hash);
    }
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();
    let env = TestEnv::builder().pkgfs(pkgfs).build().await;

    let missing = io_util::directory::open_file(
        &env.proxies.pkgfs,
        "ctl/validation/missing",
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
    )
    .await
    .unwrap();

    assert_eq!(
        io_util::file::read(&missing).await.unwrap(),
        format!("{}\n", missing_blob.unwrap()).into_bytes()
    );

    let () = env.stop().await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn expose_system_image_package_as_system_directory() {
    let blobfs = BlobfsRamdisk::start().unwrap();
    let system_image_package = SystemImageBuilder::new().build().await;
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();
    let env = TestEnv::builder().pkgfs(pkgfs).build().await;

    system_image_package.verify_contents(&env.system_dir().await).await.unwrap();

    let () = env.stop().await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn expose_pkgfs_packages_directory() {
    let blobfs = BlobfsRamdisk::start().unwrap();
    let system_image_package = SystemImageBuilder::new().build().await;
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();
    let env = TestEnv::builder().pkgfs(pkgfs).build().await;

    assert_eq!(
        files_async::readdir(&env.proxies.pkgfs_packages).await.unwrap(),
        vec![files_async::DirEntry {
            name: "system_image".to_string(),
            kind: files_async::DirentKind::Directory
        },]
    );

    let () = env.stop().await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn expose_pkgfs_versions_directory() {
    let blobfs = BlobfsRamdisk::start().unwrap();
    let system_image_package = SystemImageBuilder::new().build().await;
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();
    let env = TestEnv::builder().pkgfs(pkgfs).build().await;

    assert_eq!(
        files_async::readdir(&env.proxies.pkgfs_versions).await.unwrap(),
        vec![files_async::DirEntry {
            name: system_image_package.meta_far_merkle_root().to_string(),
            kind: files_async::DirentKind::Directory
        },]
    );

    let () = env.stop().await;
}
