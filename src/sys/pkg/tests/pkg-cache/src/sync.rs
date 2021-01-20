// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::TestEnv, blobfs_ramdisk::BlobfsRamdisk, fuchsia_async as fasync,
    fuchsia_pkg_testing::SystemImageBuilder, pkgfs_ramdisk::PkgfsRamdisk,
};

#[fasync::run_singlethreaded(test)]
async fn sync_success() {
    let blobfs = BlobfsRamdisk::start().unwrap();
    let system_image_package = SystemImageBuilder::new().build().await;
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());

    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();

    let env = TestEnv::builder().pkgfs(pkgfs).build().await;

    let res = env.proxies.package_cache.sync().await;

    assert_eq!(res.unwrap(), Ok(()));
}
