// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{get_missing_blobs, verify_fetches_succeed, write_blob, TestEnv},
    blobfs_ramdisk::BlobfsRamdisk,
    fidl_fuchsia_io::{DirectoryMarker, FileMarker},
    fidl_fuchsia_pkg::{BlobInfo, NeededBlobsMarker},
    fidl_fuchsia_pkg_ext::BlobId,
    fuchsia_async as fasync,
    fuchsia_inspect::{assert_data_tree, testing::AnyProperty, tree_assertion},
    fuchsia_pkg_testing::{Package, PackageBuilder, SystemImageBuilder},
    fuchsia_zircon::Status,
    futures::prelude::*,
    pkgfs_ramdisk::PkgfsRamdisk,
};

async fn assert_base_blob_count(
    static_packages: &[&Package],
    cache_packages: Option<&[&Package]>,
    count: u64,
) {
    let blobfs = BlobfsRamdisk::start().unwrap();
    let mut system_image_package = SystemImageBuilder::new().static_packages(static_packages);
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
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();

    let env = TestEnv::builder().pkgfs(pkgfs).build().await;
    env.block_until_started().await;

    let hierarchy = env.inspect_hierarchy().await;

    assert_data_tree!(
        hierarchy,
        root: contains {
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

async fn pkgfs_with_restrictions_enabled(restrictions_enabled: bool) -> PkgfsRamdisk {
    let blobfs = BlobfsRamdisk::start().unwrap();
    let mut system_image_package = SystemImageBuilder::new();
    if !restrictions_enabled {
        system_image_package = system_image_package.pkgfs_disable_executability_restrictions();
    }
    let system_image_package = system_image_package.build().await;
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap()
}

async fn assert_pkgfs_executability_restrictions_enabled(
    pkgfs: PkgfsRamdisk,
    expected_state: String,
) {
    let env = TestEnv::builder().pkgfs(pkgfs).build().await;
    env.block_until_started().await;

    let hierarchy = env.inspect_hierarchy().await;
    assert_data_tree!(
        hierarchy,
        root: contains {
            "pkgfs" : {
                "pkgfs-executability-restrictions-enabled": expected_state
            }
        }
    );
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn pkgfs_executability_restrictions_enabled() {
    assert_pkgfs_executability_restrictions_enabled(
        pkgfs_with_restrictions_enabled(true).await,
        "true".to_string(),
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn pkgfs_executability_restrictions_disabled() {
    assert_pkgfs_executability_restrictions_enabled(
        pkgfs_with_restrictions_enabled(false).await,
        "false".to_string(),
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn dynamic_index_inital_state() {
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

    let hierarchy = env.inspect_hierarchy().await;

    assert_data_tree!(
        hierarchy,
        root: contains {
            "index": {
                "dynamic" : {}
            }
        }
    );
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn dynamic_index_with_cache_packages() {
    let blobfs = BlobfsRamdisk::start().unwrap();
    let mut system_image_package = SystemImageBuilder::new();
    let cache_package = PackageBuilder::new("a-cache-package")
        .add_resource_at("some-cached-blob", &b"unique contents"[..])
        .build()
        .await
        .unwrap();

    system_image_package = system_image_package.cache_packages(&[&cache_package]);
    cache_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());

    let system_image_package = system_image_package.build().await;
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();

    let env = TestEnv::builder().pkgfs(pkgfs).build().await;
    env.block_until_started().await;

    let hierarchy = env.inspect_hierarchy().await;

    assert_data_tree!(
        hierarchy,
        root: contains {
            "index": {
                "dynamic": {
                    cache_package.meta_far_merkle_root().to_string() => {
                        "state" : "active",
                        "time": AnyProperty,
                        "required_blobs": 1u64,
                        "path": "a-cache-package/0",
                    },
                }
            }
        }
    );
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn dynamic_index_needed_blobs() {
    let env = TestEnv::builder().build().await;
    let pkg = PackageBuilder::new("single-blob").build().await.unwrap();

    let mut meta_blob_info =
        BlobInfo { blob_id: BlobId::from(*pkg.meta_far_merkle_root()).into(), length: 0 };

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
    let (_dir, dir_server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
    let get_fut = env
        .proxies
        .package_cache
        .get(
            &mut meta_blob_info,
            &mut std::iter::empty(),
            needed_blobs_server_end,
            Some(dir_server_end),
        )
        .map_ok(|res| res.map_err(Status::from_raw));

    let (meta_far, _) = pkg.contents();

    let (meta_blob, meta_blob_server_end) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();
    assert!(needed_blobs.open_meta_blob(meta_blob_server_end).await.unwrap().unwrap());

    let hierarchy = env.inspect_hierarchy().await;

    assert_data_tree!(
        hierarchy,
        root: contains {
            "index": {
                "dynamic": {
                    pkg.meta_far_merkle_root().to_string() => {
                        "state": "pending",
                        "time": AnyProperty,
                    }
                }
            }
        }
    );

    write_blob(&meta_far.contents, meta_blob).await;
    env.wait_for_inspect_state(tree_assertion!(
        "root": contains {
            "index": {
                "dynamic": contains {
                    pkg.meta_far_merkle_root().to_string() => contains {
                        "state": "with_meta_far",
                        "path": AnyProperty,
                        "required_blobs": AnyProperty,
                    }
                }
            }
        }
    ))
    .await;

    let hierarchy = env.inspect_hierarchy().await;
    assert_data_tree!(
        hierarchy,
        root: contains {
            "index": {
                "dynamic": {
                    pkg.meta_far_merkle_root().to_string() => {
                        "state": "with_meta_far",
                        "required_blobs": 0u64,
                        "time": AnyProperty,
                        "path": "single-blob/0",
                    }

                }
            }
        }
    );

    assert_eq!(get_missing_blobs(&needed_blobs).await, vec![]);
    let () = get_fut.await.unwrap().unwrap();
    let hierarchy = env.inspect_hierarchy().await;

    assert_data_tree!(
        hierarchy,
        root: contains {
            "index": {
                "dynamic": {
                    pkg.meta_far_merkle_root().to_string() => {
                        "state": "active",
                        "required_blobs": 0u64,
                        "time": AnyProperty,
                        "path": "single-blob/0"
                    }

                }
            }
        }
    );

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn dynamic_index_package_hash_update() {
    let env = TestEnv::builder().build().await;
    let pkg = PackageBuilder::new("single-blob").build().await.unwrap();

    let mut meta_blob_info =
        BlobInfo { blob_id: BlobId::from(*pkg.meta_far_merkle_root()).into(), length: 0 };

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
    let get_fut = env
        .proxies
        .package_cache
        .get(&mut meta_blob_info, &mut std::iter::empty(), needed_blobs_server_end, None)
        .map_ok(|res| res.map_err(Status::from_raw));

    let (meta_far, _) = pkg.contents();

    let (meta_blob, meta_blob_server_end) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();
    assert!(needed_blobs.open_meta_blob(meta_blob_server_end).await.unwrap().unwrap());

    write_blob(&meta_far.contents, meta_blob).await;

    assert_eq!(get_missing_blobs(&needed_blobs).await, vec![]);
    let () = get_fut.await.unwrap().unwrap();
    let hierarchy = env.inspect_hierarchy().await;

    assert_data_tree!(
        hierarchy,
        root: contains {
            "index": {
                "dynamic": {
                    pkg.meta_far_merkle_root().to_string() => {
                        "state": "active",
                        "required_blobs": 0u64,
                        "time": AnyProperty,
                        "path": "single-blob/0"
                    }

                }
            }
        }
    );

    let updated_pkg = PackageBuilder::new("single-blob")
        .add_resource_at("some-cached-blob", &b"updated contents"[..])
        .build()
        .await
        .unwrap();

    let updated_hash = updated_pkg.meta_far_merkle_root().to_string();
    assert_ne!(pkg.meta_far_merkle_root().to_string(), updated_hash);

    let () = verify_fetches_succeed(&env.proxies.package_cache, &[updated_pkg]).await;
    let hierarchy = env.inspect_hierarchy().await;

    assert_data_tree!(
        hierarchy,
        root: contains {
            "index": {
                "dynamic": {
                    updated_hash => {
                        "state": "active",
                        "required_blobs": 1u64,
                        "time": AnyProperty,
                        "path": "single-blob/0"
                    }

                }
            }
        }
    );
    env.stop().await;
}
