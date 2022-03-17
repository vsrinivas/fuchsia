// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::TestEnv,
    blobfs_ramdisk::BlobfsRamdisk,
    fidl_fuchsia_pkg::PackageIndexIteratorMarker,
    fidl_fuchsia_pkg_ext::BlobId,
    fuchsia_async as fasync,
    fuchsia_hash::Hash,
    fuchsia_pkg_testing::{PackageBuilder, SystemImageBuilder},
    pkgfs_ramdisk::PkgfsRamdisk,
};

async fn verify_cache_packages(
    env: &TestEnv,
    mut expected_entries: impl Iterator<Item = (String, Hash)>,
) {
    let (pkg_iterator, server_end) =
        fidl::endpoints::create_proxy::<PackageIndexIteratorMarker>().unwrap();
    env.proxies.package_cache.cache_package_index(server_end).unwrap();

    loop {
        let chunk = pkg_iterator.next().await.unwrap();
        if chunk.is_empty() {
            break;
        }
        for entry in chunk {
            let (expected_url, expected_hash) = expected_entries.next().unwrap();

            assert_eq!(entry.package_url.url, expected_url);
            assert_eq!(BlobId::from(entry.meta_far_blob_id), BlobId::from(expected_hash));
        }
    }

    assert_eq!(expected_entries.next(), None);
}

#[fasync::run_singlethreaded(test)]
async fn missing_cache_package_manifest_empty_iterator() {
    let blobfs = BlobfsRamdisk::start().unwrap();
    let system_image_package = SystemImageBuilder::new().build().await;
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();

    let env = TestEnv::builder().pkgfs(pkgfs).build().await;
    env.block_until_started().await;

    verify_cache_packages(&env, vec![].into_iter()).await;
}

#[fasync::run_singlethreaded(test)]
async fn present_cache_package_manifest() {
    let pkg = PackageBuilder::new("some-cache-package").build().await.unwrap();
    let system_image_package = SystemImageBuilder::new().cache_packages(&[&pkg]).build().await;
    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();

    let env = TestEnv::builder().pkgfs(pkgfs).build().await;
    env.block_until_started().await;

    verify_cache_packages(
        &env,
        vec![(
            "fuchsia-pkg://fuchsia.com/some-cache-package".to_string(),
            *pkg.meta_far_merkle_root(),
        )]
        .into_iter(),
    )
    .await;
}
