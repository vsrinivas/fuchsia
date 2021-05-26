// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{get_missing_blobs, verify_fetches_succeed, write_blob, TestEnv};
use fidl_fuchsia_io::{DirectoryMarker, FileMarker};
use fidl_fuchsia_pkg::{BlobInfo, NeededBlobsMarker};
use fidl_fuchsia_pkg_ext::BlobId;
use fuchsia_pkg_testing::PackageBuilder;
use fuchsia_zircon::Status;
use futures::prelude::*;

#[fuchsia_async::run_singlethreaded(test)]
async fn get_multiple_packages_with_no_content_blobs() {
    let env = TestEnv::builder().build().await;

    let packages = vec![
        PackageBuilder::new("pkg-a").build().await.unwrap(),
        PackageBuilder::new("pkg-b").build().await.unwrap(),
        PackageBuilder::new("pkg-c").build().await.unwrap(),
        PackageBuilder::new("pkg-d").build().await.unwrap(),
    ];

    let () = verify_fetches_succeed(&env.proxies.package_cache, &packages).await;

    let () = env.stop().await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn get_single_package_with_no_content_blobs() {
    let env = TestEnv::builder().build().await;
    let mut initial_blobfs_blobs = env.blobfs().list_blobs().unwrap();

    let pkg = PackageBuilder::new("single-blob").build().await.unwrap();

    let mut meta_blob_info =
        BlobInfo { blob_id: BlobId::from(*pkg.meta_far_merkle_root()).into(), length: 0 };

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
    let (dir, dir_server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
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

    write_blob(&meta_far.contents, meta_blob).await;

    assert_eq!(get_missing_blobs(&needed_blobs).await, vec![]);

    let () = get_fut.await.unwrap().unwrap();
    let () = pkg.verify_contents(&dir).await.unwrap();

    // All blobs in the package should now be present in blobfs.
    let mut expected_blobs = pkg.list_blobs().unwrap();
    expected_blobs.append(&mut initial_blobfs_blobs);
    assert_eq!(env.blobfs().list_blobs().unwrap(), expected_blobs);

    let () = env.stop().await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn get_multiple_packages_with_content_blobs() {
    let env = TestEnv::builder().build().await;

    let packages = vec![
        PackageBuilder::new("multi-content-a")
            .add_resource_at("bin/foo", "a-bin-foo".as_bytes())
            .add_resource_at("data/content", "a-data-content".as_bytes())
            .add_resource_at("data/content2", "a-data-content2".as_bytes())
            .build()
            .await
            .unwrap(),
        PackageBuilder::new("multi-content-b")
            .add_resource_at("bin/foo", "b-bin-foo".as_bytes())
            .add_resource_at("data/content", "b-data-content-same".as_bytes())
            .add_resource_at("data/content2", "b-data-content-same".as_bytes())
            .build()
            .await
            .unwrap(),
        PackageBuilder::new("multi-content-c")
            .add_resource_at("bin/foo", "c-bin-foo".as_bytes())
            .add_resource_at("data/content", "c-data-content".as_bytes())
            .add_resource_at("data/content2", "c-data-content2".as_bytes())
            .build()
            .await
            .unwrap(),
        PackageBuilder::new("multi-content-d")
            .add_resource_at("bin/foo", "d-bin-foo".as_bytes())
            .add_resource_at("data/content", "d-data-content".as_bytes())
            .add_resource_at("data/content2", "d-data-content2".as_bytes())
            .build()
            .await
            .unwrap(),
    ];

    let () = verify_fetches_succeed(&env.proxies.package_cache, &packages).await;

    let () = env.stop().await;
}
