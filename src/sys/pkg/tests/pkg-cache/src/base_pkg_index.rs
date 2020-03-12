// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::TestEnv,
    blobfs_ramdisk::BlobfsRamdisk,
    fidl_fuchsia_pkg::{
        PackageIndexIteratorMarker, PackageIndexIteratorProxy, PACKAGE_INDEX_CHUNK_SIZE,
    },
    fidl_fuchsia_pkg_ext::BlobId,
    fuchsia_async as fasync,
    fuchsia_pkg_testing::{Package, PackageBuilder, SystemImageBuilder},
    pkgfs_ramdisk::PkgfsRamdisk,
    std::convert::TryInto,
};

/// Sets up the test environment and writes the packages out to base.
async fn setup_test_env(static_packages: &[&Package]) -> TestEnv {
    let blobfs = BlobfsRamdisk::start().unwrap();
    let system_image_package = SystemImageBuilder::new(static_packages);
    let system_image_package = system_image_package.build().await;
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    for pkg in static_packages {
        pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    }
    let pkgfs = PkgfsRamdisk::start_with_blobfs(
        blobfs,
        Some(&system_image_package.meta_far_merkle_root().to_string()),
    )
    .unwrap();

    let env = TestEnv::new(pkgfs);
    env.block_until_started().await;
    env
}

/// Uses the test environment to retrieve the package iterator.
async fn get_pkg_iterator(env: &TestEnv) -> PackageIndexIteratorProxy {
    let (pkg_iterator, server_end) =
        fidl::endpoints::create_proxy::<PackageIndexIteratorMarker>().unwrap();
    env.proxies.package_cache.base_package_index(server_end).unwrap();
    pkg_iterator
}

/// Assert the iterator returns all the expected packages.
async fn assert_base_packages_match(
    pkg_iterator: PackageIndexIteratorProxy,
    static_packages: &[&Package],
) {
    let mut expected_pkg_url =
        static_packages.into_iter().map(|pkg| format!("fuchsia-pkg://fuchsia.com/{}", pkg.name()));
    let mut expected_merkle =
        static_packages.into_iter().map(|pkg| BlobId::from(pkg.meta_far_merkle_root().clone()));
    let mut chunk = pkg_iterator.next().await.unwrap();
    let max_chunk_size: usize = 32;

    while chunk.len() != 0 {
        assert!(chunk.len() <= max_chunk_size);
        for entry in chunk {
            assert_eq!(entry.package_url.url, expected_pkg_url.next().unwrap());
            assert_eq!(BlobId::from(entry.meta_far_blob_id), expected_merkle.next().unwrap());
        }
        chunk = pkg_iterator.next().await.unwrap();
    }
}

/// Verifies that a single base package is handled correctly.
#[fasync::run_singlethreaded(test)]
async fn base_pkg_index_with_one_package() {
    let pkg = PackageBuilder::new("base-package-0")
        .add_resource_at("resource", &[][..])
        .build()
        .await
        .unwrap();
    let env = setup_test_env(&[&pkg]).await;
    let pkg_iterator = get_pkg_iterator(&env).await;
    assert_base_packages_match(pkg_iterator, &[&pkg]).await;
}

/// Verifies that the internal cache is not re-ordering packages based on sorting.
#[fasync::run_singlethreaded(test)]
async fn base_pkg_index_verify_ordering() {
    let pkg_0 = PackageBuilder::new("base-package-zzz")
        .add_resource_at("resource", &[][..])
        .build()
        .await
        .unwrap();
    let pkg_1 = PackageBuilder::new("aaa-base-package")
        .add_resource_at("resource", &[][..])
        .build()
        .await
        .unwrap();
    let pkg_2 = PackageBuilder::new("base-package-aaa")
        .add_resource_at("resource", &[][..])
        .build()
        .await
        .unwrap();
    let env = setup_test_env(&[&pkg_0, &pkg_1, &pkg_2]).await;
    let pkg_iterator = get_pkg_iterator(&env).await;
    assert_base_packages_match(pkg_iterator, &[&pkg_0, &pkg_1, &pkg_2]).await;
}

/// Verifies that the package index can be split across multiple chunks.
#[fasync::run_singlethreaded(test)]
async fn base_pkg_index_verify_multiple_chunks() {
    let bundle_size = (PACKAGE_INDEX_CHUNK_SIZE * 3 + 1).try_into().unwrap();
    let mut larger_bundle = Vec::<Package>::with_capacity(bundle_size);
    // A non-chunk aligned value is selected to ensure we get the remainder.
    for i in 0..bundle_size {
        larger_bundle.push(
            PackageBuilder::new(format!("base-package-{}", i))
                .add_resource_at("resource", &[][..])
                .build()
                .await
                .unwrap(),
        );
    }
    let env = setup_test_env(&larger_bundle.iter().collect::<Vec<&Package>>()).await;
    let pkg_iterator = get_pkg_iterator(&env).await;
    assert_base_packages_match(pkg_iterator, &larger_bundle.iter().collect::<Vec<&Package>>())
        .await;
}
