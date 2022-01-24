// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        replace_retained_packages, verify_fetches_succeed, verify_packages_cached, write_meta_far,
        write_needed_blobs, TestEnv,
    },
    assert_matches::assert_matches,
    blobfs_ramdisk::BlobfsRamdisk,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_pkg::{BlobInfo, NeededBlobsMarker},
    fidl_fuchsia_pkg_ext::BlobId,
    fuchsia_pkg_testing::{PackageBuilder, SystemImageBuilder},
    fuchsia_zircon as zx,
    futures::TryFutureExt,
    pkgfs_ramdisk::PkgfsRamdisk,
};

#[fuchsia_async::run_singlethreaded(test)]
async fn cached_packages_are_retained() {
    // Packages to be retained.
    let packages = vec![
        PackageBuilder::new("pkg-a").build().await.unwrap(),
        PackageBuilder::new("multi-pkg-a")
            .add_resource_at("bin/foo", "a-bin-foo".as_bytes())
            .add_resource_at("data/content", "a-data-content".as_bytes())
            .build()
            .await
            .unwrap(),
    ];

    // Packages to be written to BlobFS to emulate data available for GC.
    let garbage_packages = vec![
        PackageBuilder::new("pkg-b").build().await.unwrap(),
        PackageBuilder::new("multi-pkg-b")
            .add_resource_at("bin/bar", "b-bin-bar".as_bytes())
            .add_resource_at("data/asset", "b-data-asset".as_bytes())
            .build()
            .await
            .unwrap(),
    ];

    let blobfs = BlobfsRamdisk::start().unwrap();
    let system_image_package = SystemImageBuilder::new().build().await;
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    for pkg in packages.iter().chain(garbage_packages.iter()) {
        pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    }
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();

    let env = TestEnv::builder().pkgfs(pkgfs).build().await;

    let blob_ids =
        packages.iter().map(|pkg| BlobId::from(*pkg.meta_far_merkle_root())).collect::<Vec<_>>();

    // Mark packages as retained.
    replace_retained_packages(&env.proxies.retained_packages, &blob_ids.as_slice()).await;

    assert_matches!(env.proxies.space_manager.gc().await, Ok(Ok(())));

    // Verify no retained package blobs are deleted, directly from blobfs.
    for pkg in packages.iter() {
        assert!(env.blobfs().client().has_blob(pkg.meta_far_merkle_root()).await);
    }

    for pkg in garbage_packages.iter() {
        assert_eq!(env.blobfs().client().has_blob(pkg.meta_far_merkle_root()).await, false);
    }

    // Verify no retained package blobs are deleted, using PackageCache API.
    verify_packages_cached(&env.proxies.package_cache, &packages).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn packages_are_retained_gc_mid_process() {
    let env = TestEnv::builder().build().await;
    let package = PackageBuilder::new("multi-pkg-a")
        .add_resource_at("bin/foo", "a-bin-foo".as_bytes())
        .add_resource_at("data/content", "a-data-content".as_bytes())
        .build()
        .await
        .unwrap();

    let blob_id = BlobId::from(*package.meta_far_merkle_root());

    // Start installing a package (write the meta far).
    let mut meta_blob_info = BlobInfo { blob_id: blob_id.into(), length: 0 };

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
    let (dir, dir_server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
    let get_fut = env
        .proxies
        .package_cache
        .get(&mut meta_blob_info, needed_blobs_server_end, Some(dir_server_end))
        .map_ok(|res| res.map_err(zx::Status::from_raw));

    let (meta_far, contents) = package.contents();
    write_meta_far(&needed_blobs, meta_far).await;

    // Add the packages as retained.
    replace_retained_packages(&env.proxies.retained_packages, &vec![blob_id.into()]).await;

    // Clear the retained index and GC.
    assert_matches!(env.proxies.retained_packages.clear().await, Ok(()));
    assert_matches!(env.proxies.space_manager.gc().await, Ok(Ok(())));

    // Verify the package’s meta far is not deleted.
    assert!(env.blobfs().client().has_blob(package.meta_far_merkle_root()).await);

    write_needed_blobs(&needed_blobs, contents).await;

    assert_matches!(env.proxies.space_manager.gc().await, Ok(Ok(())));

    let () = get_fut.await.unwrap().unwrap();
    let () = package.verify_contents(&dir).await.unwrap();
}

#[fuchsia_async::run_singlethreaded(test)]
async fn cached_and_released_packages_are_removed() {
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

    // Mark packages as retained.
    replace_retained_packages(&env.proxies.retained_packages, &blob_ids.as_slice()).await;

    // Cache the packages.
    let () = verify_fetches_succeed(&env.proxies.package_cache, &packages).await;

    assert_matches!(env.proxies.space_manager.gc().await, Ok(Ok(())));

    // Verify no retained package blobs are deleted, directly from blobfs.
    for pkg in packages.iter() {
        assert!(env.blobfs().client().has_blob(pkg.meta_far_merkle_root()).await);
    }

    // Verify no retained package blobs are deleted, using PackageCache API.
    verify_packages_cached(&env.proxies.package_cache, &packages).await;

    // Clear all retained blobs and GC.
    assert_matches!(env.proxies.retained_packages.clear().await, Ok(()));
    assert_matches!(env.proxies.space_manager.gc().await, Ok(Ok(())));

    for package in packages.iter() {
        assert_eq!(
            env.open_package(&package.meta_far_merkle_root().to_string()).await.map(|_| ()),
            Err(zx::Status::NOT_FOUND)
        )
    }
}
