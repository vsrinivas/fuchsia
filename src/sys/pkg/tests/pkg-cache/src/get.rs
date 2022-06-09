// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{do_fetch, get_missing_blobs, verify_fetches_succeed, write_blob, TestEnv},
    assert_matches::assert_matches,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_pkg::{BlobInfo, BlobInfoIteratorMarker, NeededBlobsMarker},
    fidl_fuchsia_pkg_ext::BlobId,
    fuchsia_merkle::MerkleTree,
    fuchsia_pkg_testing::{PackageBuilder, SystemImageBuilder},
    fuchsia_zircon::Status,
    futures::prelude::*,
};

#[fuchsia_async::run_singlethreaded(test)]
async fn get_multiple_packages_with_no_content_blobs() {
    let env = TestEnv::builder().build().await;

    let packages = vec![
        PackageBuilder::new("pkg-a").build().await.unwrap(),
        PackageBuilder::new("pkg-b").build().await.unwrap(),
        PackageBuilder::new("pkg-c").build().await.unwrap(),
        PackageBuilder::new("pkg-d").build().await.unwrap(),
    ];

    // Assert the packages are not present before the get
    for pkg in &packages {
        assert_eq!(
            env.open_package(&pkg.meta_far_merkle_root().to_string()).await.map(|_| ()),
            Err(Status::NOT_FOUND)
        );
    }

    let () = verify_fetches_succeed(&env.proxies.package_cache, &packages).await;

    let () = env.stop().await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn get_single_package_with_no_content_blobs() {
    let env = TestEnv::builder().build().await;
    let mut initial_blobfs_blobs = env.blobfs.list_blobs().unwrap();

    let pkg = PackageBuilder::new("single-blob").build().await.unwrap();

    let mut meta_blob_info =
        BlobInfo { blob_id: BlobId::from(*pkg.meta_far_merkle_root()).into(), length: 0 };

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
    let (dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let get_fut = env
        .proxies
        .package_cache
        .get(&mut meta_blob_info, needed_blobs_server_end, Some(dir_server_end))
        .map_ok(|res| res.map_err(Status::from_raw));

    let (meta_far, _) = pkg.contents();

    let (meta_blob, meta_blob_server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
    assert!(needed_blobs.open_meta_blob(meta_blob_server_end).await.unwrap().unwrap());

    let () = write_blob(&meta_far.contents, meta_blob).await.unwrap();

    assert_eq!(get_missing_blobs(&needed_blobs).await, vec![]);

    let () = get_fut.await.unwrap().unwrap();
    let () = pkg.verify_contents(&dir).await.unwrap();

    // All blobs in the package should now be present in blobfs.
    let mut expected_blobs = pkg.list_blobs().unwrap();
    expected_blobs.append(&mut initial_blobfs_blobs);
    assert_eq!(env.blobfs.list_blobs().unwrap(), expected_blobs);

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

#[fuchsia_async::run_singlethreaded(test)]
async fn get_and_hold_directory() {
    let env = TestEnv::builder().build().await;

    let package = PackageBuilder::new("pkg-a").build().await.unwrap();

    // Request and write a package, hold the package directory.
    let dir = do_fetch(&env.proxies.package_cache, &package).await;

    let mut meta_blob_info =
        BlobInfo { blob_id: BlobId::from(*package.meta_far_merkle_root()).into(), length: 0 };

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();

    let (dir_2, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    // Request same package again.
    let get_fut = env
        .proxies
        .package_cache
        .get(&mut meta_blob_info, needed_blobs_server_end, Some(dir_server_end))
        .map_ok(|res| res.map_err(Status::from_raw));

    // `OpenMetaBlob()` for already cached package closes the channel with with a `ZX_OK` epitaph.
    let (_meta_blob, meta_blob_server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
    assert_matches!(
        needed_blobs.open_meta_blob(meta_blob_server_end).await,
        Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. })
    );

    let () = get_fut.await.unwrap().unwrap();

    // Verify package directories from both requests.
    let () = package.verify_contents(&dir).await.unwrap();
    let () = package.verify_contents(&dir_2).await.unwrap();
}

#[fuchsia_async::run_singlethreaded(test)]
async fn unavailable_when_client_drops_needed_blobs_channel() {
    let env = TestEnv::builder().build().await;

    let pkg = PackageBuilder::new("pkg-a").build().await.unwrap();

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

    drop(needed_blobs);

    assert_eq!(get_fut.await.unwrap(), Err(Status::UNAVAILABLE));
}

#[fuchsia_async::run_singlethreaded(test)]
async fn handles_partially_written_pkg() {
    let env = TestEnv::builder().build().await;
    let blobfs = blobfs::Client::new(env.blobfs.root_dir_proxy().unwrap());

    let pkg = PackageBuilder::new("partially-written")
        .add_resource_at("written-1", &b"some contents"[..])
        .add_resource_at("not-written-1", &b"different contents"[..])
        .build()
        .await
        .unwrap();

    let meta_far_hash = *pkg.meta_far_merkle_root();
    let meta_far_data = {
        use std::io::Read as _;

        let mut bytes = vec![];
        pkg.meta_far().unwrap().read_to_end(&mut bytes).unwrap();
        bytes
    };

    let meta_blob_info = BlobInfo { blob_id: BlobId::from(meta_far_hash).into(), length: 0 };

    // Write the meta far to blobfs.
    let _ = blobfs
        .open_blob_for_write(&meta_far_hash)
        .await
        .unwrap()
        .truncate(meta_far_data.len() as u64)
        .await
        .unwrap()
        .unwrap_needs_data()
        .write(&meta_far_data)
        .await
        .unwrap()
        .unwrap_done();

    // Write a content blob to blobfs.
    {
        let data = &b"some contents"[..];
        let hash = MerkleTree::from_reader(data).unwrap().root();
        let _ = blobfs
            .open_blob_for_write(&hash)
            .await
            .unwrap()
            .truncate(data.len() as u64)
            .await
            .unwrap()
            .unwrap_needs_data()
            .write(data)
            .await
            .unwrap()
            .unwrap_done();
    }

    // Perform a Get(), expecting to only write the 1 remaining content blob.
    let dir = {
        let data = &b"different contents"[..];
        let hash = MerkleTree::from_reader(data).unwrap().root();

        let pkg_cache = env.client();
        let mut get = pkg_cache.get(meta_blob_info.into()).unwrap();

        assert_matches!(get.open_meta_blob().await.unwrap(), None);
        let missing = get.get_missing_blobs().await.unwrap();
        assert_eq!(
            missing,
            vec![fidl_fuchsia_pkg_ext::BlobInfo { blob_id: hash.into(), length: 0 }]
        );

        let blob = get.open_blob(hash.into()).await.unwrap().unwrap();
        let (blob, closer) = (blob.blob, blob.closer);
        let blob = blob.truncate(data.len() as u64).await.unwrap();
        assert_matches!(
            blob.write(data).await.unwrap(),
            fidl_fuchsia_pkg_ext::cache::BlobWriteSuccess::Done
        );
        closer.close().await;

        get.finish().await.unwrap()
    };

    let () = pkg.verify_contents(&dir.into_proxy()).await.unwrap();

    let () = env.stop().await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn get_package_already_present_on_fs() {
    let pkg = PackageBuilder::new("some-package")
        .add_resource_at("some-blob", &b"some contents"[..])
        .build()
        .await
        .unwrap();
    let system_image_package = SystemImageBuilder::new().cache_packages(&[&pkg]).build().await;
    let env = TestEnv::builder()
        .blobfs_from_system_image_and_extra_packages(&system_image_package, &[&pkg])
        .build()
        .await;

    let mut meta_blob_info =
        BlobInfo { blob_id: BlobId::from(*pkg.meta_far_merkle_root()).into(), length: 0 };

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();

    let (dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();

    // Call `PackageCache.Get()` for already cached package.
    let get_fut = env
        .proxies
        .package_cache
        .get(&mut meta_blob_info, needed_blobs_server_end, Some(dir_server_end))
        .map_ok(|res| res.map_err(Status::from_raw));

    // `OpenMetaBlob()` for already cached package closes the channel with with a `ZX_OK` epitaph.
    let (_meta_blob, meta_blob_server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
    assert_matches!(
        needed_blobs.open_meta_blob(meta_blob_server_end).await,
        Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. })
    );

    // The remote end sends the epitaph, and then at some point later, closes the channel.
    // We check for both here to account for the channel not yet being closed when the
    // `GetMissingBlobs` call occurs.
    let (_blob_iterator, blob_iterator_server_end) =
        fidl::endpoints::create_proxy::<BlobInfoIteratorMarker>().unwrap();
    assert_matches!(
        needed_blobs.get_missing_blobs(blob_iterator_server_end),
        Ok(()) | Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. })
    );

    let () = get_fut.await.unwrap().unwrap();

    // `dir` is resolved to package directory.
    let () = pkg.verify_contents(&dir).await.unwrap();

    let () = env.stop().await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn get_package_already_present_on_fs_with_pre_closed_needed_blobs() {
    let pkg = PackageBuilder::new("some-package")
        .add_resource_at("some-blob", &b"some contents"[..])
        .build()
        .await
        .unwrap();
    let system_image_package = SystemImageBuilder::new().cache_packages(&[&pkg]).build().await;
    let env = TestEnv::builder()
        .blobfs_from_system_image_and_extra_packages(&system_image_package, &[&pkg])
        .build()
        .await;

    let mut meta_blob_info =
        BlobInfo { blob_id: BlobId::from(*pkg.meta_far_merkle_root()).into(), length: 0 };

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
    let (pkgdir, pkgdir_server_end) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();

    drop(needed_blobs);

    // Call `PackageCache.Get()` for already cached package.
    let get_fut = env
        .proxies
        .package_cache
        .get(&mut meta_blob_info, needed_blobs_server_end, Some(pkgdir_server_end))
        .map_ok(|res| res.map_err(Status::from_raw));

    let () = get_fut.await.unwrap().unwrap();

    let () = pkg.verify_contents(&pkgdir).await.unwrap();

    let () = env.stop().await;
}
