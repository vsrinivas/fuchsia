// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::TestEnv, blobfs_ramdisk::BlobfsRamdisk, fuchsia_pkg_testing::SystemImageBuilder,
    pkgfs_ramdisk::PkgfsRamdisk,
};

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
