// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::TestEnv,
    blobfs_ramdisk::BlobfsRamdisk,
    fidl_fuchsia_pkg::{PackageIndexIteratorMarker, PackageIndexIteratorProxy},
    fidl_fuchsia_pkg_ext::BlobId,
    fuchsia_async as fasync,
    fuchsia_hash::{Hash, HashRangeFull},
    fuchsia_pkg::PackagePath,
    fuchsia_pkg_testing::{Package, PackageBuilder, SystemImageBuilder},
    pkgfs_ramdisk::PkgfsRamdisk,
};

/// Sets up the test environment and writes the packages out to base.
async fn setup_test_env(static_packages: &[&Package]) -> TestEnv {
    let blobfs = BlobfsRamdisk::start().unwrap();
    let system_image_package = SystemImageBuilder::new().static_packages(static_packages);
    let system_image_package = system_image_package.build().await;
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    for pkg in static_packages {
        pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    }
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();

    let env = TestEnv::builder().pkgfs(pkgfs).build();
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
    let expected_entries = static_packages.into_iter().map(|pkg| {
        let url = format!("fuchsia-pkg://fuchsia.com/{}", pkg.name());
        let merkle = pkg.meta_far_merkle_root().clone();
        (url, merkle)
    });

    verify_base_packages_iterator(pkg_iterator, expected_entries).await;
}

async fn verify_base_packages_iterator(
    pkg_iterator: PackageIndexIteratorProxy,
    mut expected_entries: impl Iterator<Item = (String, Hash)>,
) {
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
    // Try to get a partial chunk by using an unaligned value, though the server may choose to
    // provide fewer entries in a single chunk.
    const PACKAGE_INDEX_CHUNK_SIZE_MAX: usize = 818;
    let bundle_size = PACKAGE_INDEX_CHUNK_SIZE_MAX * 3 + 1;

    let mut system_image = SystemImageBuilder::new();
    let mut expected_entries = Vec::with_capacity(bundle_size);

    for (i, hash) in HashRangeFull::default().enumerate().take(bundle_size) {
        let name = format!("base-package-{}", i);
        let url = format!("fuchsia-pkg://fuchsia.com/{}", name);
        let path = PackagePath::from_name_and_variant(name, "0").unwrap();

        expected_entries.push((url, hash));
        system_image = system_image.static_package(path, hash);
    }

    let blobfs = BlobfsRamdisk::start().unwrap();
    let system_image = system_image.build().await;

    system_image.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image.meta_far_merkle_root())
        .start()
        .unwrap();

    let env = TestEnv::builder().pkgfs(pkgfs).build();

    let pkg_iterator = get_pkg_iterator(&env).await;
    verify_base_packages_iterator(pkg_iterator, expected_entries.into_iter()).await;
}
