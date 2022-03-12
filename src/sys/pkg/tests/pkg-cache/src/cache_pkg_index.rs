// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{do_fetch, TestEnv},
    assert_matches::assert_matches,
    blobfs_ramdisk::BlobfsRamdisk,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_pkg::PackageIndexIteratorMarker,
    fidl_fuchsia_pkg_ext::BlobId,
    fuchsia_async as fasync,
    fuchsia_hash::Hash,
    fuchsia_pkg_testing::{PackageBuilder, SystemImageBuilder},
    fuchsia_zircon_status as zx_status,
    io_util::directory::{open_directory, open_file},
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

/// Allows us to call sort inline
fn sorted<T: Ord>(mut vec: Vec<T>) -> Vec<T> {
    vec.sort();
    vec
}

async fn ls(dir: &fio::DirectoryProxy, path: &str) -> Result<Vec<String>, files_async::Error> {
    let d = &open_directory(&dir, path, fio::OPEN_RIGHT_READABLE).await.unwrap();
    Ok(files_async::readdir(&d).await?.into_iter().map(|dir_entry| dir_entry.name).collect())
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

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn present_cache_package_manifest() {
    let pkg = PackageBuilder::new("example")
        .add_resource_at("a/b", "Hello world!\n".as_bytes())
        .build()
        .await
        .expect("build package");

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
        vec![("fuchsia-pkg://fuchsia.com/example".to_string(), *pkg.meta_far_merkle_root())]
            .into_iter(),
    )
    .await;

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_with_cache_index() {
    let pkg = PackageBuilder::new("example")
        .add_resource_at("a/b", "Hello world!\n".as_bytes())
        .build()
        .await
        .expect("build package");
    let system_image_package = SystemImageBuilder::new()
        .cache_packages(&[&pkg])
        .pkgfs_non_static_packages_allowlist(&["example"])
        .build()
        .await;

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

    let d = &env.proxies.pkgfs;

    system_image_package
        .verify_contents(&open_directory(d, "system", fio::OPEN_RIGHT_READABLE).await.unwrap())
        .await
        .expect("valid system_image");

    assert_eq!(sorted(ls(&d, "packages").await.unwrap()), ["example", "system_image"]);

    pkg.verify_contents(
        &open_directory(d, "packages/example/0", fio::OPEN_RIGHT_READABLE).await.unwrap(),
    )
    .await
    .expect("valid example package");

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_with_cache_index_missing_cache_meta_far() {
    let pkg = PackageBuilder::new("example")
        .add_resource_at("a/b", "Hello world!\n".as_bytes())
        .build()
        .await
        .expect("build package");
    let system_image_package = SystemImageBuilder::new()
        .cache_packages(&[&pkg])
        .pkgfs_non_static_packages_allowlist(&["example"])
        .build()
        .await;

    let blobfs = BlobfsRamdisk::start().unwrap();

    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());

    // NB: package is not written to blobfs.

    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();

    let env = TestEnv::builder().pkgfs(pkgfs).build().await;
    env.block_until_started().await;

    let d = &env.proxies.pkgfs;

    system_image_package
        .verify_contents(&open_directory(d, "system", fio::OPEN_RIGHT_READABLE).await.unwrap())
        .await
        .expect("valid system_image");

    assert_eq!(ls(&d, "packages").await.unwrap(), ["system_image"]);

    assert_matches!(
        open_file(d, "packages/example/0/meta", fio::OPEN_RIGHT_READABLE).await,
        Err(io_util::node::OpenError::OpenError(zx_status::Status::NOT_FOUND))
    );

    assert_matches!(
        open_file(
            d,
            format!("versions/{}/meta", pkg.meta_far_merkle_root()).as_str(),
            fio::OPEN_RIGHT_READABLE
        )
        .await,
        Err(io_util::node::OpenError::OpenError(zx_status::Status::NOT_FOUND))
    );

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_with_cache_index_missing_cache_content_blob() {
    let pkg = PackageBuilder::new("example")
        .add_resource_at("a/b", "Hello world!\n".as_bytes())
        .build()
        .await
        .expect("build package");
    let system_image_package = SystemImageBuilder::new()
        .cache_packages(&[&pkg])
        .pkgfs_non_static_packages_allowlist(&["example"])
        .build()
        .await;

    let blobfs = BlobfsRamdisk::start().unwrap();

    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    blobfs.add_blob_from(&pkg.meta_far_merkle_root(), pkg.meta_far().unwrap()).unwrap();

    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();

    let env = TestEnv::builder().pkgfs(pkgfs).build().await;
    env.block_until_started().await;

    let d = &env.proxies.pkgfs;

    system_image_package
        .verify_contents(&open_directory(d, "system", fio::OPEN_RIGHT_READABLE).await.unwrap())
        .await
        .expect("valid system_image");

    assert_eq!(ls(&d, "packages").await.unwrap(), ["system_image"]);

    assert_matches!(
        open_file(d, "packages/example/0/meta", fio::OPEN_RIGHT_READABLE).await,
        Err(io_util::node::OpenError::OpenError(zx_status::Status::NOT_FOUND))
    );

    assert_matches!(
        open_file(
            d,
            format!("versions/{}/meta", pkg.meta_far_merkle_root()).as_str(),
            fio::OPEN_RIGHT_READABLE
        )
        .await,
        Err(io_util::node::OpenError::OpenError(zx_status::Status::NOT_FOUND))
    );

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_restart_reveals_shadowed_cache_package() {
    let pkg = PackageBuilder::new("example")
        .add_resource_at("a/b", "Hello world!\n".as_bytes())
        .build()
        .await
        .expect("build package");
    let system_image_package = SystemImageBuilder::new()
        .cache_packages(&[&pkg])
        .pkgfs_non_static_packages_allowlist(&["example"])
        .build()
        .await;

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

    let d = &env.proxies.pkgfs;

    let pkg2 = PackageBuilder::new("example")
        .add_resource_at("a/b", "Hello world 2!\n".as_bytes())
        .build()
        .await
        .expect("build package");

    // Request and write a package.
    let _ = do_fetch(&env.proxies.package_cache, &pkg2).await;

    pkg2.verify_contents(
        &open_directory(&d, "packages/example/0", fio::OPEN_RIGHT_READABLE).await.unwrap(),
    )
    .await
    .expect("pkg2 replaced pkg");

    // cache version is inaccessible
    assert_matches!(
        open_file(
            d,
            format!("versions/{}", pkg.meta_far_merkle_root()).as_str(),
            fio::OPEN_RIGHT_READABLE
        )
        .await,
        Err(io_util::node::OpenError::OpenError(zx_status::Status::NOT_FOUND))
    );

    let pkgfs = env.pkgfs.restart().unwrap();

    let env = TestEnv::builder().pkgfs(pkgfs).build().await;
    env.block_until_started().await;

    let d = &env.proxies.pkgfs;

    // cache version is accessible again.
    pkg.verify_contents(
        &open_directory(&d, "packages/example/0", fio::OPEN_RIGHT_READABLE).await.unwrap(),
    )
    .await
    .unwrap();

    pkg.verify_contents(
        &open_directory(
            &d,
            &format!("versions/{}", pkg.meta_far_merkle_root()),
            fio::OPEN_RIGHT_READABLE,
        )
        .await
        .unwrap(),
    )
    .await
    .unwrap();

    // updated version is gone
    assert_matches!(
        open_file(
            d,
            format!("versions/{}", pkg2.meta_far_merkle_root()).as_str(),
            fio::OPEN_RIGHT_READABLE
        )
        .await,
        Err(io_util::node::OpenError::OpenError(zx_status::Status::NOT_FOUND))
    );

    env.stop().await;
}
