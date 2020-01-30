// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::TestEnv,
    blobfs_ramdisk::BlobfsRamdisk,
    fuchsia_async as fasync,
    fuchsia_inspect::assert_inspect_tree,
    fuchsia_pkg_testing::{Package, PackageBuilder, SystemImageBuilder},
    pkgfs_ramdisk::PkgfsRamdisk,
};

async fn assert_base_blob_count(
    static_packages: &[&Package],
    cache_packages: Option<&[&Package]>,
    count: u64,
) {
    let blobfs = BlobfsRamdisk::start().unwrap();
    let mut system_image_package = SystemImageBuilder::new(static_packages);
    if let Some(cache_packages) = cache_packages {
        system_image_package = system_image_package.cache_packages(cache_packages);
        for pkg in cache_packages {
            pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
        }
    }
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

    let hierarchy = env.inspect_hierarchy().await;

    assert_inspect_tree!(
        hierarchy,
        root: {
            "blob-location": {
                "base-blobs": {
                    count: count,
                }
            }
        }
    );
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn base_blob_count_with_empty_system_image() {
    // system_image: meta.far and data/static_packages (the empty blob)
    assert_base_blob_count(&[], None, 2).await;
}

#[fasync::run_singlethreaded(test)]
async fn base_blob_count_with_one_base_package() {
    let pkg = PackageBuilder::new("a-base-package")
        .add_resource_at("some-empty-blob", &[][..])
        .build()
        .await
        .unwrap();
    // system_image: meta.far and data/static_packages (no longer the empty blob)
    // a-base-package: meta.far and some-empty-blob
    assert_base_blob_count(&[&pkg], None, 4).await;
}

#[fasync::run_singlethreaded(test)]
async fn base_blob_count_with_two_base_packages_and_one_shared_blob() {
    let pkg0 = PackageBuilder::new("a-base-package")
        .add_resource_at("some-blob", &b"shared-contents"[..])
        .build()
        .await
        .unwrap();
    let pkg1 = PackageBuilder::new("other-base-package")
        .add_resource_at("this-blob-is-shared-with-pkg0", &b"shared-contents"[..])
        .build()
        .await
        .unwrap();
    // system_image: meta.far and data/static_packages (no longer the empty blob)
    // a-base-package: meta.far and some-blob
    // other-base-package: meta.far (this-blob-is-shared-with-pkg0 is shared)
    assert_base_blob_count(&[&pkg0, &pkg1], None, 5).await;
}

#[fasync::run_singlethreaded(test)]
async fn base_blob_count_ignores_cache_packages() {
    let pkg = PackageBuilder::new("a-cache-package")
        .add_resource_at("some-cached-blob", &b"unique contents"[..])
        .build()
        .await
        .unwrap();
    // system_image: meta.far, data/static_packages (empty), data/cache_packages (non-empty)
    // a-cache-package: ignored
    assert_base_blob_count(&[], Some(&[&pkg]), 3).await;
}

#[fasync::run_singlethreaded(test)]
async fn assume_all_blobs_in_base_on_error() {
    // no system_image in pkgfs, so BlobLocation will use fallback.
    let env = TestEnv::new(PkgfsRamdisk::start().unwrap());
    env.block_until_started().await;

    let hierarchy = env.inspect_hierarchy().await;
    assert_inspect_tree!(
        hierarchy,
        root: {
            "blob-location": {
                "assume-all-in-base": {}
            }
        }
    );
    env.stop().await;
}
