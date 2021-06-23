// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{do_fetch, get_missing_blobs, verify_fetches_succeed, write_blob, TestEnv},
    blobfs_ramdisk::BlobfsRamdisk,
    fidl_fuchsia_io::{DirectoryMarker, FileMarker},
    fidl_fuchsia_pkg::{BlobInfo, BlobInfoIteratorMarker, NeededBlobsMarker},
    fidl_fuchsia_pkg_ext::BlobId,
    fuchsia_merkle::MerkleTree,
    fuchsia_pkg_testing::{PackageBuilder, SystemImageBuilder},
    fuchsia_zircon::Status,
    futures::prelude::*,
    matches::assert_matches,
    pkgfs_ramdisk::PkgfsRamdisk,
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

    let () = write_blob(&meta_far.contents, meta_blob).await.unwrap();

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

    let (dir_2, dir_server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
    // Request same package again.
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

    // `OpenMetaBlob()` for already cached package closes the channel with with a `ZX_OK` epitaph.
    let (_meta_blob, meta_blob_server_end) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();
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

    drop(needed_blobs);

    assert_eq!(get_fut.await.unwrap(), Err(Status::UNAVAILABLE));
}

#[fuchsia_async::run_singlethreaded(test)]
async fn recovers_from_inconsistent_pkgfs_state() {
    let env = TestEnv::builder().build().await;
    let pkgfs_root = env.pkgfs.root_dir_proxy().unwrap();
    let pkgfs_install = pkgfs::install::Client::open_from_pkgfs_root(&pkgfs_root).unwrap();

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

    // Write the meta far through pkgfs.
    {
        let (blob, closer) = pkgfs_install
            .create_blob(meta_far_hash.into(), pkgfs::install::BlobKind::Package)
            .await
            .unwrap();

        let blob = blob.truncate(meta_far_data.len() as u64).await.unwrap();
        assert_matches!(
            blob.write(&meta_far_data).await.unwrap(),
            pkgfs::install::BlobWriteSuccess::Done
        );
        closer.close().await;
    }

    // Write a content blob through pkgfs.
    {
        let data = &b"some contents"[..];
        let hash = MerkleTree::from_reader(data).unwrap().root();
        let (blob, closer) =
            pkgfs_install.create_blob(hash, pkgfs::install::BlobKind::Data).await.unwrap();

        let blob = blob.truncate(data.len() as u64).await.unwrap();
        assert_matches!(blob.write(data).await.unwrap(), pkgfs::install::BlobWriteSuccess::Done);
        closer.close().await;
    }

    // Perform a Get(), expecting to only write the 1 remaining content blob and for the package to
    // activate in pkgfs as expected.
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
    let blobfs = BlobfsRamdisk::start().unwrap();

    let system_image_package = SystemImageBuilder::new().cache_packages(&[&pkg]);
    pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());

    let system_image_package = system_image_package.build().await;
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());

    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();

    let env = TestEnv::builder().pkgfs(pkgfs).build().await;

    let mut meta_blob_info =
        BlobInfo { blob_id: BlobId::from(*pkg.meta_far_merkle_root()).into(), length: 0 };

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();

    let (dir, dir_server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();

    // Call `PackageCache.Get()` for already cached package.
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

    // `OpenMetaBlob()` for already cached package closes the channel with with a `ZX_OK` epitaph.
    let (_meta_blob, meta_blob_server_end) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();
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
