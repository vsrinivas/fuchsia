// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests pkg-resolver's behavior when resolving base packages.
use {
    assert_matches::assert_matches,
    fuchsia_pkg_testing::serve::responder,
    fuchsia_pkg_testing::{Package, PackageBuilder, RepositoryBuilder, SystemImageBuilder},
    fuchsia_zircon::Status,
    futures::future,
    lib::{TestEnvBuilder, EMPTY_REPO_PATH},
    std::sync::Arc,
};

async fn test_package(name: &str, contents: &str) -> Package {
    PackageBuilder::new(name)
        .add_resource_at("p/t/o", format!("contents: {contents}\n").as_bytes())
        .build()
        .await
        .expect("build package")
}

#[fuchsia::test]
async fn test_base_package_found() {
    let pkg_name = "test_base_package_found";
    let base_pkg = test_package(pkg_name, "static").await;
    // Put a copy of the package with altered contents in the repo to make sure
    // we're getting the one from the system image.
    let repo_pkg = test_package(pkg_name, "repo").await;
    let system_image_package =
        SystemImageBuilder::new().static_packages(&[&base_pkg]).build().await;
    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&base_pkg])
        .build()
        .await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&repo_pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo.server().start().unwrap();

    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;
    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{pkg_name}");
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
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

#[fuchsia::test]
async fn test_base_pinning_rejects_urls_with_resource() {
    let pkg_name = "test_base_pinning_rejects_urls_with_resource";
    let pkg = test_package(pkg_name, "static").await;
    let system_image_package = SystemImageBuilder::new().static_packages(&[&pkg]).build().await;
    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&pkg])
        .build()
        .await;

    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{pkg_name}/0#should-not-be-here");
    assert_matches!(
        env.resolve_package(&pkg_url).await,
        Err(fidl_fuchsia_pkg::ResolveError::InvalidUrl)
    );
    assert_matches!(env.get_hash(pkg_url).await, Err(Status::INVALID_ARGS));

    env.stop().await;
}

#[fuchsia::test]
async fn test_base_package_with_variant_found() {
    let pkg_name = "test_base_package_with_variant_found";
    let base_pkg = test_package(pkg_name, "static").await;
    // Put a copy of the package with altered contents in the repo to make sure
    // we're getting the one from the system image.
    let repo_pkg = test_package(pkg_name, "repo").await;
    let system_image_package =
        SystemImageBuilder::new().static_packages(&[&base_pkg]).build().await;
    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&base_pkg])
        .build()
        .await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&repo_pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo.server().start().unwrap();

    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;
    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{pkg_name}/0");
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    // Make sure we got the static version, not the repo version.
    base_pkg.verify_contents(&package_dir).await.unwrap();
    assert!(repo_pkg.verify_contents(&package_dir).await.is_err());

    // Check that get_hash fallback behavior matches resolve.
    let hash = env.get_hash(pkg_url).await;
    assert_eq!(hash.unwrap(), base_pkg.meta_far_merkle_root().clone().into());

    env.stop().await;
}

// The package is in the static index, but it has a Merkle pin.
// Use the resolver instead of the base index.
#[fuchsia::test]
async fn test_base_package_with_merkle_pin() {
    let pkg_name = "test_base_package_with_merkle_pin";
    let base_pkg = test_package(pkg_name, "static").await;
    // Put a copy of the package with altered contents in the repo to make sure
    // we're getting the one from the repo.
    let repo_pkg = test_package(pkg_name, "repo").await;
    let system_image_package =
        SystemImageBuilder::new().static_packages(&[&base_pkg]).build().await;
    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&base_pkg])
        .build()
        .await;

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
        format!("fuchsia-pkg://fuchsia.com/{pkg_name}?hash={}", repo_pkg.meta_far_merkle_root());
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();

    // Make sure we got the repo (pinned) version, not the static version.
    repo_pkg.verify_contents(&package_dir).await.unwrap();
    assert!(base_pkg.verify_contents(&package_dir).await.is_err());

    // Check that get_hash fallback behavior matches resolve.
    let hash = env.get_hash(pkg_url).await;
    assert_eq!(hash.unwrap(), repo_pkg.meta_far_merkle_root().clone().into());

    env.stop().await;
}

#[fuchsia::test]
async fn test_base_package_while_tuf_broken() {
    let pkg_name = "test_base_package_while_tuf_broken";
    let base_pkg = test_package(pkg_name, "static").await;
    let system_image_package =
        SystemImageBuilder::new().static_packages(&[&base_pkg]).build().await;
    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&base_pkg])
        .build()
        .await;

    let repo =
        Arc::new(RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH).build().await.unwrap());
    let served_repository = repo.server().start().unwrap();

    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;

    served_repository.stop().await;

    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{pkg_name}");
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    // Make sure we got the static version.
    base_pkg.verify_contents(&package_dir).await.unwrap();

    // Check that get_hash fallback behavior matches resolve.
    let hash = env.get_hash(pkg_url).await;
    assert_eq!(hash.unwrap(), base_pkg.meta_far_merkle_root().clone().into());

    env.stop().await;
}

#[fuchsia::test]
async fn test_base_package_without_repo_configured() {
    let pkg_name = "test_base_package_without_repo_configured";
    let base_pkg = test_package(pkg_name, "static").await;
    let system_image_package =
        SystemImageBuilder::new().static_packages(&[&base_pkg]).build().await;
    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&base_pkg])
        .build()
        .await;

    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{pkg_name}");
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    // Make sure we got the static version.
    base_pkg.verify_contents(&package_dir).await.unwrap();

    // Check that get_hash fallback behavior matches resolve.
    let hash = env.get_hash(pkg_url).await;
    assert_eq!(hash.unwrap(), base_pkg.meta_far_merkle_root().clone().into());

    env.stop().await;
}

#[fuchsia::test]
async fn test_base_package_while_queue_full() {
    let mut repo = RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH);
    let mut pkg_urls: Vec<String> = vec![];
    // MAX_CONCURRENT_PACKAGE_FETCHES is currently set to 5.
    // Add 5 packages that would fill up the worker queue.
    for i in 1..=5 {
        let pkg_name = format!("blob_causes_timeout{i}");
        pkg_urls.push(format!("fuchsia-pkg://test/{pkg_name}"));
        repo = repo.add_package(PackageBuilder::new(pkg_name).build().await.unwrap());
    }
    let repo = Arc::new(repo.build().await.unwrap());

    let pkg_name = "test_base_package_while_queue_full";
    let base_pkg = test_package(pkg_name, "static").await;
    let system_image_package =
        SystemImageBuilder::new().static_packages(&[&base_pkg]).build().await;
    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&base_pkg])
        .build()
        .await;

    let server = repo
        .server()
        .response_overrider(responder::ForPathPrefix::new("/blobs/", responder::Hang))
        .start()
        .expect("Starting server succeeds");
    let repo_config = server.make_repo_config("fuchsia-pkg://test".parse().unwrap());
    env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    // Add a base-pinned package to the end of the resolve list.
    pkg_urls.push(format!("fuchsia-pkg://fuchsia.com/{pkg_name}"));

    let resolves_fut =
        pkg_urls.into_iter().map(|url| Box::pin(env.resolve_package(&url))).collect::<Vec<_>>();

    let (res, i, _) = future::select_all(resolves_fut).await;
    // Assert it's the base pinned package which got resolved.
    assert_eq!(i, 5);

    // Make sure we got the static version.
    let (pkg_dir, _resolved_context) = res.unwrap();
    base_pkg.verify_contents(&pkg_dir).await.unwrap();

    env.stop().await;
}
