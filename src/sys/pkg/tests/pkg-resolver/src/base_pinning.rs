// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests pkg-resolver's behavior when resolving base packages.
use {
    blobfs_ramdisk::BlobfsRamdisk,
    fuchsia_async as fasync,
    fuchsia_pkg_testing::{Package, PackageBuilder, RepositoryBuilder, SystemImageBuilder},
    fuchsia_zircon::Status,
    lib::{TestEnvBuilder, EMPTY_REPO_PATH},
    matches::assert_matches,
    pkgfs_ramdisk::PkgfsRamdisk,
    std::sync::Arc,
};

async fn test_package(name: &str, contents: &str) -> Package {
    PackageBuilder::new(name)
        .add_resource_at("p/t/o", format!("contents: {}\n", contents).as_bytes())
        .build()
        .await
        .expect("build package")
}

async fn pkgfs_with_system_image_and_pkg(
    system_image_package: &Package,
    pkg: Option<&Package>,
) -> PkgfsRamdisk {
    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    if let Some(pkg) = pkg {
        pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    }
    PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap()
}

#[fasync::run_singlethreaded(test)]
async fn test_base_package_found() {
    let pkg_name = "test_base_package_found";
    let base_pkg = test_package(pkg_name, "static").await;
    // Put a copy of the package with altered contents in the repo to make sure
    // we're getting the one from the system image.
    let repo_pkg = test_package(pkg_name, "repo").await;
    let system_image_package =
        SystemImageBuilder::new().static_packages(&[&base_pkg]).build().await;
    let pkgfs = pkgfs_with_system_image_and_pkg(&system_image_package, Some(&base_pkg)).await;

    let env = TestEnvBuilder::new().pkgfs(pkgfs).build().await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&repo_pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo.server().start().unwrap();

    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;
    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", pkg_name);
    let package_dir = env.resolve_package(&pkg_url).await.unwrap();
    // Make sure we got the base version, not the repo version.
    base_pkg.verify_contents(&package_dir).await.unwrap();
    assert!(repo_pkg.verify_contents(&package_dir).await.is_err());

    // Make sure that repo_pkg is not cached locally.
    assert_eq!(
        env.open_cached_package(repo_pkg.meta_far_merkle_root().clone().into())
            .await
            .expect_err("repo_pkg should not be cached"),
        Status::NOT_FOUND
    );

    // Check that get_hash fallback behavior matches resolve.
    let hash = env.get_hash(pkg_url).await;
    assert_eq!(hash.unwrap(), base_pkg.meta_far_merkle_root().clone().into());

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn test_base_pinning_rejects_urls_with_resource() {
    let pkg_name = "test_base_pinning_rejects_urls_with_resource";
    let pkg = test_package(pkg_name, "static").await;
    let system_image_package = SystemImageBuilder::new().static_packages(&[&pkg]).build().await;
    let pkgfs = pkgfs_with_system_image_and_pkg(&system_image_package, Some(&pkg)).await;
    let env = TestEnvBuilder::new().pkgfs(pkgfs).build().await;

    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}/0#should-not-be-here", pkg_name,);
    assert_matches!(env.resolve_package(&pkg_url).await, Err(Status::INVALID_ARGS));
    assert_matches!(env.get_hash(pkg_url).await, Err(Status::INVALID_ARGS));

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn test_base_package_with_variant_found() {
    let pkg_name = "test_base_package_with_variant_found";
    let base_pkg = test_package(pkg_name, "static").await;
    // Put a copy of the package with altered contents in the repo to make sure
    // we're getting the one from the system image.
    let repo_pkg = test_package(pkg_name, "repo").await;
    let system_image_package =
        SystemImageBuilder::new().static_packages(&[&base_pkg]).build().await;
    let pkgfs = pkgfs_with_system_image_and_pkg(&system_image_package, Some(&base_pkg)).await;

    let env = TestEnvBuilder::new().pkgfs(pkgfs).build().await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&repo_pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo.server().start().unwrap();

    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;
    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}/0", pkg_name);
    let package_dir = env.resolve_package(&pkg_url).await.unwrap();
    // Make sure we got the static version, not the repo version.
    base_pkg.verify_contents(&package_dir).await.unwrap();
    assert!(repo_pkg.verify_contents(&package_dir).await.is_err());

    // Check that get_hash fallback behavior matches resolve.
    let hash = env.get_hash(pkg_url).await;
    assert_eq!(hash.unwrap(), base_pkg.meta_far_merkle_root().clone().into());

    env.stop().await;
}

// The package is in the static index, but not in pkgfs.
#[fasync::run_singlethreaded(test)]
async fn test_absent_base_package() {
    let pkg_name = "test_absent_base_package";
    let base_pkg = test_package(pkg_name, "static").await;
    // Put a copy of the package with altered contents in the repo to make sure
    // we're getting the one from the system image.
    let repo_pkg = test_package(pkg_name, "repo").await;
    let system_image_package =
        SystemImageBuilder::new().static_packages(&[&base_pkg]).build().await;
    let pkgfs = pkgfs_with_system_image_and_pkg(&system_image_package, None).await;

    let env = TestEnvBuilder::new().pkgfs(pkgfs).build().await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&repo_pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo.server().start().unwrap();

    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;
    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", pkg_name);
    // Resolve returns NOT FOUND if cache.open() fails.
    let res = env.resolve_package(&pkg_url).await;
    assert_matches!(res, Err(Status::NOT_FOUND));

    // GetHash will succeed because it doesn't try to open the package.
    let hash = env.get_hash(pkg_url).await;
    assert_eq!(hash.unwrap(), base_pkg.meta_far_merkle_root().clone().into());

    env.stop().await;
}

// The package is in the static index, but it has a Merkle pin.
// Use the resolver instead of the base index.
#[fasync::run_singlethreaded(test)]
async fn test_base_package_with_merkle_pin() {
    let pkg_name = "test_base_package_with_merkle_pin";
    let base_pkg = test_package(pkg_name, "static").await;
    // Put a copy of the package with altered contents in the repo to make sure
    // we're getting the one from the repo.
    let repo_pkg = test_package(pkg_name, "repo").await;
    let system_image_package =
        SystemImageBuilder::new().static_packages(&[&base_pkg]).build().await;
    let pkgfs = pkgfs_with_system_image_and_pkg(&system_image_package, Some(&base_pkg)).await;

    let env = TestEnvBuilder::new().pkgfs(pkgfs).build().await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&repo_pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo.server().start().unwrap();

    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;
    // Merkle pin the request to the repo version.
    let pkg_url =
        format!("fuchsia-pkg://fuchsia.com/{}?hash={}", pkg_name, repo_pkg.meta_far_merkle_root());
    let package_dir = env.resolve_package(&pkg_url).await.unwrap();

    // Make sure we got the repo (pinned) version, not the static version.
    repo_pkg.verify_contents(&package_dir).await.unwrap();
    assert!(base_pkg.verify_contents(&package_dir).await.is_err());

    // Check that get_hash fallback behavior matches resolve.
    let hash = env.get_hash(pkg_url).await;
    assert_eq!(hash.unwrap(), repo_pkg.meta_far_merkle_root().clone().into());

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn test_base_package_while_tuf_broken() {
    let pkg_name = "test_base_package_while_tuf_broken";
    let base_pkg = test_package(pkg_name, "static").await;
    let system_image_package =
        SystemImageBuilder::new().static_packages(&[&base_pkg]).build().await;
    let pkgfs = pkgfs_with_system_image_and_pkg(&system_image_package, Some(&base_pkg)).await;

    let env = TestEnvBuilder::new().pkgfs(pkgfs).build().await;

    let repo =
        Arc::new(RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH).build().await.unwrap());
    let served_repository = repo.server().start().unwrap();

    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;

    served_repository.stop().await;

    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", pkg_name);
    let package_dir = env.resolve_package(&pkg_url).await.unwrap();
    // Make sure we got the static version.
    base_pkg.verify_contents(&package_dir).await.unwrap();

    // Check that get_hash fallback behavior matches resolve.
    let hash = env.get_hash(pkg_url).await;
    assert_eq!(hash.unwrap(), base_pkg.meta_far_merkle_root().clone().into());

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn test_base_package_without_repo_configured() {
    let pkg_name = "test_base_package_without_repo_configured";
    let base_pkg = test_package(pkg_name, "static").await;
    let system_image_package =
        SystemImageBuilder::new().static_packages(&[&base_pkg]).build().await;
    let pkgfs = pkgfs_with_system_image_and_pkg(&system_image_package, Some(&base_pkg)).await;

    let env = TestEnvBuilder::new().pkgfs(pkgfs).build().await;

    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", pkg_name);
    let package_dir = env.resolve_package(&pkg_url).await.unwrap();
    // Make sure we got the static version.
    base_pkg.verify_contents(&package_dir).await.unwrap();

    // Check that get_hash fallback behavior matches resolve.
    let hash = env.get_hash(pkg_url).await;
    assert_eq!(hash.unwrap(), base_pkg.meta_far_merkle_root().clone().into());

    env.stop().await;
}
