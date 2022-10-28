// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        get_and_verify_packages, get_missing_blobs, replace_retained_packages, write_blob,
        write_meta_far, write_needed_blobs, TestEnv,
    },
    assert_matches::assert_matches,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_pkg::{BlobInfo, NeededBlobsMarker, PackageCacheMarker},
    fidl_fuchsia_pkg_ext::BlobId,
    fuchsia_async as fasync,
    fuchsia_inspect::{
        assert_data_tree, hierarchy::DiagnosticsHierarchy, testing::AnyProperty, tree_assertion,
    },
    fuchsia_pkg_testing::{Package, PackageBuilder, SystemImageBuilder},
    fuchsia_zircon as zx,
    fuchsia_zircon::Status,
    futures::prelude::*,
    std::collections::HashMap,
};

#[fasync::run_singlethreaded(test)]
async fn system_image_hash_present() {
    let system_image_package = SystemImageBuilder::new().build().await;
    let env = TestEnv::builder().blobfs_from_system_image(&system_image_package).build().await;
    env.block_until_started().await;

    let hierarchy = env.inspect_hierarchy().await;
    assert_data_tree!(
        hierarchy,
        root: contains {
            "system_image": system_image_package.meta_far_merkle_root().to_string(),
        }
    );
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn system_image_hash_ignored() {
    let env = TestEnv::builder().ignore_system_image().build().await;
    env.block_until_started().await;

    let hierarchy = env.inspect_hierarchy().await;
    assert_data_tree!(
        hierarchy,
        root: contains {
            "system_image": "ignored",
        }
    );
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn non_static_allow_list() {
    let system_image_package = SystemImageBuilder::new()
        .pkgfs_non_static_packages_allowlist(&["a-package-name", "another-name"])
        .build()
        .await;
    let env = TestEnv::builder().blobfs_from_system_image(&system_image_package).build().await;
    env.block_until_started().await;

    let hierarchy = env.inspect_hierarchy().await;
    assert_data_tree!(
        hierarchy,
        "root": contains {
            "non_static_allow_list": {
                "a-package-name": "",
                "another-name": "",
            },
        }
    );
    env.stop().await;
}

async fn assert_base_blob_count(
    static_packages: &[&Package],
    cache_packages: Option<&[&Package]>,
    count: u64,
) {
    let mut system_image_package = SystemImageBuilder::new().static_packages(static_packages);
    if let Some(cache_packages) = cache_packages {
        system_image_package = system_image_package.cache_packages(cache_packages);
    }
    let system_image_package = system_image_package.build().await;
    let env = TestEnv::builder()
        .blobfs_from_system_image_and_extra_packages(
            &system_image_package,
            &static_packages
                .iter()
                .cloned()
                .chain(cache_packages.unwrap_or(&[]).iter().cloned())
                .collect::<Vec<_>>(),
        )
        .build()
        .await;
    env.block_until_started().await;

    let hierarchy = env.inspect_hierarchy().await;

    assert_data_tree!(
        hierarchy,
        root: contains {
            "base-packages": {
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
async fn executability_restrictions_enabled() {
    let env = TestEnv::builder().build().await;
    env.block_until_started().await;

    let hierarchy = env.inspect_hierarchy().await;

    assert_data_tree!(
        hierarchy,
        root: contains {
            "executability-restrictions": "Enforce".to_string(),
        }
    );
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn executability_restrictions_disabled() {
    let system_image_package =
        SystemImageBuilder::new().pkgfs_disable_executability_restrictions().build().await;
    let env = TestEnv::builder().blobfs_from_system_image(&system_image_package).build().await;
    env.block_until_started().await;

    let hierarchy = env.inspect_hierarchy().await;

    assert_data_tree!(
        hierarchy,
        root: contains {
            "executability-restrictions": "DoNotEnforce".to_string(),
        }
    );
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn dynamic_index_inital_state() {
    let system_image_package = SystemImageBuilder::new().build().await;
    let env = TestEnv::builder().blobfs_from_system_image(&system_image_package).build().await;
    env.block_until_started().await;

    let hierarchy = env.inspect_hierarchy().await;

    assert_data_tree!(
        hierarchy,
        root: contains {
            "index": contains {
                "dynamic" : {}
            }
        }
    );
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn dynamic_index_with_cache_packages() {
    let cache_package = PackageBuilder::new("a-cache-package")
        .add_resource_at("some-cached-blob", &b"unique contents"[..])
        .build()
        .await
        .unwrap();

    let system_image_package =
        SystemImageBuilder::new().cache_packages(&[&cache_package]).build().await;

    let env = TestEnv::builder()
        .blobfs_from_system_image_and_extra_packages(&system_image_package, &[&cache_package])
        .build()
        .await;
    env.block_until_started().await;

    let hierarchy = env.inspect_hierarchy().await;

    assert_data_tree!(
        hierarchy,
        root: contains {
            "index": contains {
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
    let (_dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let get_fut = env
        .proxies
        .package_cache
        .get(&mut meta_blob_info, needed_blobs_server_end, Some(dir_server_end))
        .map_ok(|res| res.map_err(Status::from_raw));

    let (meta_far, _) = pkg.contents();

    let (meta_blob, meta_blob_server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
    assert!(needed_blobs.open_meta_blob(meta_blob_server_end).await.unwrap().unwrap());

    let hierarchy = env.inspect_hierarchy().await;

    assert_data_tree!(
        hierarchy,
        root: contains {
            "index": contains {
                "dynamic": {
                    pkg.meta_far_merkle_root().to_string() => {
                        "state": "pending",
                        "time": AnyProperty,
                    }
                }
            }
        }
    );

    let () = write_blob(&meta_far.contents, meta_blob).await.unwrap();
    env.wait_for_and_return_inspect_state(tree_assertion!(
        "root": contains {
            "index": contains {
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
            "index": contains {
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
            "index": contains {
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
        .get(&mut meta_blob_info, needed_blobs_server_end, None)
        .map_ok(|res| res.map_err(Status::from_raw));

    let (meta_far, _) = pkg.contents();

    let (meta_blob, meta_blob_server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
    assert!(needed_blobs.open_meta_blob(meta_blob_server_end).await.unwrap().unwrap());

    let () = write_blob(&meta_far.contents, meta_blob).await.unwrap();

    assert_eq!(get_missing_blobs(&needed_blobs).await, vec![]);
    let () = get_fut.await.unwrap().unwrap();
    let hierarchy = env.inspect_hierarchy().await;

    assert_data_tree!(
        hierarchy,
        root: contains {
            "index": contains {
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

    let () = get_and_verify_packages(&env.proxies.package_cache, &[updated_pkg]).await;
    let hierarchy = env.inspect_hierarchy().await;

    assert_data_tree!(
        hierarchy,
        root: contains {
            "index": contains {
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

#[fasync::run_singlethreaded(test)]
async fn package_cache_get() {
    async fn expect_and_return_inspect(env: &TestEnv, state: &'static str) -> DiagnosticsHierarchy {
        let hierarchy = env
            .wait_for_and_return_inspect_state(tree_assertion!(
                "root": contains {
                    "fuchsia.pkg.PackageCache": contains {
                        "get": contains {
                            "0": contains {
                                "state": state
                            }
                        }
                    }
                }
            ))
            .await;

        assert_data_tree!(
            &hierarchy,
            root: contains {
                "fuchsia.pkg.PackageCache": {
                    "get": {
                        "0" : contains {
                            "state": state,
                            "started-time": AnyProperty,
                            "meta-far-id":
                                "311ff6cebeff1e3d4c4dbe79c340caee13acea24ad69a20a7dd23b521b8db8c9",
                            "meta-far-length": 42u64,
                        }
                    }
                }
            }
        );

        hierarchy
    }

    fn contains_missing_blob_stats(
        hierarchy: &DiagnosticsHierarchy,
        remaining: u64,
        writing: u64,
        written: u64,
    ) {
        assert_data_tree!(
            hierarchy,
            root: contains {
                "fuchsia.pkg.PackageCache": {
                    "get": {
                        "0" : contains {
                            "known_remaining": remaining,
                            "writing": writing,
                            "written": written,
                        }
                    }
                }
            }
        );
    }

    let env = TestEnv::builder().build().await;
    let package = PackageBuilder::new("multi-pkg-a")
        .add_resource_at("bin/foo", "a-bin-foo".as_bytes())
        .add_resource_at("data/content", "a-data-content".as_bytes())
        .build()
        .await
        .unwrap();

    let mut meta_blob_info =
        BlobInfo { blob_id: BlobId::from(*package.meta_far_merkle_root()).into(), length: 42 };

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
    let (_dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let get_fut = env
        .proxies
        .package_cache
        .get(&mut meta_blob_info, needed_blobs_server_end, Some(dir_server_end))
        .map_ok(|res| res.map_err(zx::Status::from_raw));

    // Request received, expect client requesting meta far.

    expect_and_return_inspect(&env, "need-meta-far").await;

    // Expect client fulfilling meta far.

    let (meta_far, contents) = package.contents();
    write_meta_far(&needed_blobs, meta_far).await;

    // Meta far done, expect client requesting missing blobs.

    expect_and_return_inspect(&env, "enumerate-missing-blobs").await;

    let missing_blobs = get_missing_blobs(&needed_blobs).await;

    // Missing blobs requested, expect client writing content blobs.

    let hierarchy = expect_and_return_inspect(&env, "need-content-blobs").await;
    contains_missing_blob_stats(&hierarchy, 2, 0, 0);

    let mut contents = contents
        .into_iter()
        .map(|(hash, bytes)| (BlobId::from(hash), bytes))
        .collect::<HashMap<_, Vec<u8>>>();

    let mut missing_blobs_iter = missing_blobs.into_iter();
    let mut blob = missing_blobs_iter.next().unwrap();

    let buf = contents.remove(&blob.blob_id.into()).unwrap();
    let (content_blob, content_blob_server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

    assert!(needed_blobs
        .open_blob(&mut blob.blob_id, content_blob_server_end)
        .await
        .unwrap()
        .unwrap());

    // Content blob open for writing.

    let hierarchy = expect_and_return_inspect(&env, "need-content-blobs").await;
    contains_missing_blob_stats(&hierarchy, 2, 1, 0);

    let () = write_blob(&buf, content_blob).await.unwrap();

    // Content blob written.

    let hierarchy = expect_and_return_inspect(&env, "need-content-blobs").await;
    contains_missing_blob_stats(&hierarchy, 1, 0, 1);

    let mut blob = missing_blobs_iter.next().unwrap();

    let buf = contents.remove(&blob.blob_id.into()).unwrap();
    let (content_blob, content_blob_server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

    assert_eq!(
        true,
        needed_blobs.open_blob(&mut blob.blob_id, content_blob_server_end).await.unwrap().unwrap()
    );

    // Last content blob open for writing.

    let hierarchy = expect_and_return_inspect(&env, "need-content-blobs").await;
    contains_missing_blob_stats(&hierarchy, 1, 1, 1);

    let () = write_blob(&buf, content_blob).await.unwrap();

    // Last content blob written.

    assert_eq!(contents, Default::default());
    assert_eq!(None, missing_blobs_iter.next());

    let () = get_fut.await.unwrap().unwrap();

    let hierarchy = env.inspect_hierarchy().await;
    assert_data_tree!(
        hierarchy,
        root: contains {
            "fuchsia.pkg.PackageCache": {
                "get": {}
            }
        }
    );

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn package_cache_concurrent_gets() {
    let package = PackageBuilder::new("a-blob").build().await.unwrap();
    let package2 = PackageBuilder::new("b-blob").build().await.unwrap();
    let env = TestEnv::builder().build().await;

    let blob_id = BlobId::from(*package.meta_far_merkle_root());
    let mut meta_blob_info = BlobInfo { blob_id: blob_id.into(), length: 42 };

    let (_needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
    let (_dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let _get_fut = env
        .proxies
        .package_cache
        .get(&mut meta_blob_info, needed_blobs_server_end, Some(dir_server_end))
        .map_ok(|res| res.map_err(zx::Status::from_raw));

    // Initiate concurrent connection to `PackageCache`.
    let package_cache_proxy2 = env
        .apps
        .realm_instance
        .root
        .connect_to_protocol_at_exposed_dir::<PackageCacheMarker>()
        .expect("connect to package cache");

    let blob_id2 = BlobId::from(*package2.meta_far_merkle_root());
    let mut meta_blob_info2 = BlobInfo { blob_id: blob_id2.into(), length: 7 };
    let (_needed_blobs2, needed_blobs_server_end2) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
    let (_dir, dir_server_end2) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let _get_fut = package_cache_proxy2
        .get(&mut meta_blob_info2, needed_blobs_server_end2, Some(dir_server_end2))
        .map_ok(|res| res.map_err(zx::Status::from_raw));

    let hierarchy = env
        .wait_for_and_return_inspect_state(tree_assertion!(
            "root": contains {
                "fuchsia.pkg.PackageCache": contains {
                    "get": contains {
                        "0": contains {
                            "state": AnyProperty,
                        },
                        "1": contains {
                            "state": AnyProperty,
                        }
                    }
                }
            }
        ))
        .await;
    assert_data_tree!(
        &hierarchy,
        root: contains {
            "fuchsia.pkg.PackageCache": {
                "get": {
                    "0" : {
                        "state": "need-meta-far",
                        "started-time": AnyProperty,
                        "meta-far-id": AnyProperty,
                        "meta-far-length": AnyProperty,
                    },
                    "1" : {
                        "state": "need-meta-far",
                        "started-time": AnyProperty,
                        "meta-far-id": AnyProperty,
                        "meta-far-length": AnyProperty,
                    }
                }
            }
        }
    );

    let values = ["0", "1"]
        .iter()
        .map(|i| {
            let node =
                hierarchy.get_child_by_path(&["fuchsia.pkg.PackageCache", "get", i]).unwrap();
            let length =
                node.get_property("meta-far-length").and_then(|property| property.uint()).unwrap();
            let hash =
                node.get_property("meta-far-id").and_then(|property| property.string()).unwrap();
            (hash, length)
        })
        .collect::<Vec<_>>();

    assert_matches!(
        values[..],
        [
            ("0e2fe90610179abbd7e648f0526419d48e226aabaef32e52d79b4cfa4187c880", 42u64),
            ("31ec29010d421b02992dff997a05786bd000aed369ffa77749c8629fdbeacf05", 7u64)
        ] | [
            ("31ec29010d421b02992dff997a05786bd000aed369ffa77749c8629fdbeacf05", 7u64),
            ("0e2fe90610179abbd7e648f0526419d48e226aabaef32e52d79b4cfa4187c880", 42u64)
        ]
    );
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn retained_index_inital_state() {
    let env = TestEnv::builder().build().await;

    let hierarchy = env.inspect_hierarchy().await;
    assert_data_tree!(
        hierarchy,
        root: contains {
            "index": contains {
                "retained" : {
                    "generation" : 0u64,
                    "last-set" : AnyProperty,
                    "entries" : {},
                }
            }
        }
    );
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn retained_index_updated_and_persisted() {
    let env = TestEnv::builder().build().await;
    let packages = vec![
        PackageBuilder::new("pkg-a").build().await.unwrap(),
        PackageBuilder::new("multi-pkg-a")
            .add_resource_at("bin/foo", "a-bin-foo".as_bytes())
            .add_resource_at("data/content", "a-data-content".as_bytes())
            .build()
            .await
            .unwrap(),
    ];
    let blob_ids =
        packages.iter().map(|pkg| BlobId::from(*pkg.meta_far_merkle_root())).collect::<Vec<_>>();

    replace_retained_packages(&env.proxies.retained_packages, blob_ids.as_slice()).await;

    let mut meta_blob_info = BlobInfo { blob_id: blob_ids[0].into(), length: 0 };

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
    let (_dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let get_fut = env
        .proxies
        .package_cache
        .get(&mut meta_blob_info, needed_blobs_server_end, Some(dir_server_end))
        .map_ok(|res| res.map_err(zx::Status::from_raw));

    let hierarchy = env.inspect_hierarchy().await;
    assert_data_tree!(
        hierarchy,
        root: contains {
            "index": contains {
                "retained" : {
                    "generation" : 1u64,
                    "last-set" : AnyProperty,
                    "entries" : {
                        blob_ids[0].to_string() => {
                            "last-set": AnyProperty,
                            "state": "need-meta-far",
                        },
                        blob_ids[1].to_string() => {
                            "last-set": AnyProperty,
                            "state": "need-meta-far",
                        }
                    },
                }
            }
        }
    );

    let (meta_far, _contents) = packages[0].contents();
    write_meta_far(&needed_blobs, meta_far).await;

    // Writing meta far triggers index update, however that's done concurrently
    // with the test, and there's no clear signal available through FIDL API.

    env.wait_for_and_return_inspect_state(tree_assertion!(
        root: contains {
            "index": contains {
                "retained" : {
                    "generation" : 1u64,
                    "last-set" : AnyProperty,
                    "entries" : contains {
                        blob_ids[0].to_string() => {
                            "last-set": AnyProperty,
                            "state": "known",
                            "blobs-count": 0u64
                        }
                    },
                }
            }
        }
    ))
    .await;

    let mut meta_blob_info2 =
        BlobInfo { blob_id: BlobId::from(*packages[1].meta_far_merkle_root()).into(), length: 0 };

    let (needed_blobs2, needed_blobs_server_end2) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
    let (_dir2, dir_server_end2) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let get_fut2 = env
        .proxies
        .package_cache
        .get(&mut meta_blob_info2, needed_blobs_server_end2, Some(dir_server_end2))
        .map_ok(|res| res.map_err(zx::Status::from_raw));

    let (meta_far2, _contents2) = packages[1].contents();
    write_meta_far(&needed_blobs2, meta_far2).await;

    // Writing meta far triggers index update, however that's done concurrently
    // with the test, and there's no clear signal available through FIDL API.

    env.wait_for_and_return_inspect_state(tree_assertion!(
        root: contains {
            "index": contains {
                "retained" : {
                    "generation" : 1u64,
                    "last-set" : AnyProperty,
                    "entries" : contains {
                        blob_ids[1].to_string() => {
                            "last-set": AnyProperty,
                            "state": "known",
                            "blobs-count": 2u64
                        },
                    },
                }
            }
        }
    ))
    .await;

    drop(needed_blobs);
    drop(needed_blobs2);
    assert_matches!(get_fut.await.unwrap(), Err(_));
    assert_matches!(get_fut2.await.unwrap(), Err(_));

    let hierarchy = env.inspect_hierarchy().await;
    assert_data_tree!(
        hierarchy,
        root: contains {
            "index": contains {
                "retained" : {
                    "generation" : 1u64,
                    "last-set" : AnyProperty,
                    "entries" : {
                        blob_ids[0].to_string() => {
                            "last-set": AnyProperty,
                            "state": "known",
                            "blobs-count": 0u64
                        },
                        blob_ids[1].to_string() => {
                            "last-set": AnyProperty,
                            "state": "known",
                            "blobs-count": 2u64
                        },
                    },
                }
            }
        }
    );
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn index_updated_mid_package_write() {
    let env = TestEnv::builder().build().await;
    let package = PackageBuilder::new("multi-pkg-a")
        .add_resource_at("bin/foo", "a-bin-foo".as_bytes())
        .add_resource_at("data/content", "a-data-content".as_bytes())
        .build()
        .await
        .unwrap();
    let blob_id = BlobId::from(*package.meta_far_merkle_root());
    let mut meta_blob_info = BlobInfo { blob_id: blob_id.into(), length: 0 };

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
    let (dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let get_fut = env
        .proxies
        .package_cache
        .get(&mut meta_blob_info, needed_blobs_server_end, Some(dir_server_end))
        .map_ok(|res| res.map_err(zx::Status::from_raw));

    let (meta_far, contents) = package.contents();
    write_meta_far(&needed_blobs, meta_far).await;

    replace_retained_packages(&env.proxies.retained_packages, &[blob_id]).await;

    // Writing meta far triggers index update, however that's done concurrently
    // with the test, and there's no clear signal available through FIDL API.

    env.wait_for_and_return_inspect_state(tree_assertion!(
        root: contains {
            "index": contains {
                "retained" : {
                    "generation" : 1u64,
                    "last-set" : AnyProperty,
                    "entries" : {
                        blob_id.to_string() => {
                            "last-set": AnyProperty,
                            "state": "known",
                            "blobs-count": 2u64,
                        },
                    },
                }
            }
        }
    ))
    .await;

    write_needed_blobs(&needed_blobs, contents).await;
    let () = get_fut.await.unwrap().unwrap();
    let () = package.verify_contents(&dir).await.unwrap();

    let hierarchy = env.inspect_hierarchy().await;
    assert_data_tree!(
        hierarchy,
        root: contains {
            "index": {
                "dynamic": {
                    blob_id.to_string() => {
                        "state" : "active",
                        "time": AnyProperty,
                        "required_blobs": 2u64,
                        "path": "multi-pkg-a/0",
                    },
                },
                "retained" : {
                    "generation" : 1u64,
                    "last-set" : AnyProperty,
                    "entries" : {
                        blob_id.to_string() => {
                            "last-set": AnyProperty,
                            "state": "known",
                            "blobs-count": 2u64,
                        },
                    },
                }
            }
        }
    );
    env.stop().await;
}
