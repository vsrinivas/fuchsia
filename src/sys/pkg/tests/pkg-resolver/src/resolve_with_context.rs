// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    assert_matches::assert_matches,
    fidl_fuchsia_pkg as fpkg, fidl_fuchsia_pkg_ext as pkg,
    fuchsia_pkg_testing::{PackageBuilder, RepositoryBuilder, SystemImageBuilder},
    lib::{TestEnvBuilder, EMPTY_REPO_PATH},
    std::sync::Arc,
};

#[fuchsia::test]
async fn absolute_url_rejects_non_empty_context() {
    let env = TestEnvBuilder::new().build().await;
    let pkg = PackageBuilder::new("package-name").build().await.unwrap();
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = Arc::clone(&repo).server().start().unwrap();
    let repo_config = served_repository.make_repo_config("fuchsia-pkg://test".parse().unwrap());
    let () = env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    // Fails with non-empty context
    assert_matches!(
        env.resolve_with_context(
            "fuchsia-pkg://test/package-name",
            [0u8; 32].as_slice().try_into().unwrap()
        )
        .await,
        Err(fpkg::ResolveError::InvalidContext)
    );

    // Succeeds with empty context
    let (resolved_pkg, _) = env
        .resolve_with_context("fuchsia-pkg://test/package-name", pkg::ResolutionContext::new())
        .await
        .unwrap();
    pkg.verify_contents(&resolved_pkg).await.unwrap();

    env.stop().await;
}

#[fuchsia::test]
async fn relative_url_succeeds() {
    let env = TestEnvBuilder::new().enable_subpackages().build().await;
    let subpackage = PackageBuilder::new("subpackage")
        .add_resource_at("subpackage-blob", "subpackage-blob-contents".as_bytes())
        .build()
        .await
        .unwrap();
    let superpackage = PackageBuilder::new("superpackage")
        .add_subpackage("my-subpackage", &subpackage)
        .build()
        .await
        .unwrap();
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&superpackage)
            .add_package(&subpackage)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = Arc::clone(&repo).server().start().unwrap();
    let repo_config = served_repository.make_repo_config("fuchsia-pkg://test".parse().unwrap());
    let () = env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();
    let (_, context) = env
        .resolve_package("fuchsia-pkg://test/superpackage")
        .await
        .expect("package to resolve without error");

    let (resolved_subpackage, _) =
        env.resolve_with_context("my-subpackage", context).await.unwrap();

    subpackage.verify_contents(&resolved_subpackage).await.unwrap();

    env.stop().await;
}

#[fuchsia::test]
async fn subpackage_of_a_subpackage() {
    let env = TestEnvBuilder::new().enable_subpackages().build().await;
    let subsubpackage = PackageBuilder::new("subsubpackage")
        .add_resource_at("subsubpackage-blob", "subsubpackage-blob-contents".as_bytes())
        .build()
        .await
        .unwrap();
    let subpackage = PackageBuilder::new("subpackage")
        .add_resource_at("subpackage-blob", "subpackage-blob-contents".as_bytes())
        .add_subpackage("my-subsubpackage", &subsubpackage)
        .build()
        .await
        .unwrap();
    let superpackage = PackageBuilder::new("superpackage")
        .add_subpackage("my-subpackage", &subpackage)
        .build()
        .await
        .unwrap();
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&superpackage)
            .add_package(&subpackage)
            .add_package(&subsubpackage)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = Arc::clone(&repo).server().start().unwrap();
    let repo_config = served_repository.make_repo_config("fuchsia-pkg://test".parse().unwrap());
    let () = env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();
    let (_, context) = env
        .resolve_package("fuchsia-pkg://test/superpackage")
        .await
        .expect("package to resolve without error");
    let (_, subcontext) = env.resolve_with_context("my-subpackage", context).await.unwrap();

    let (resolved_subsubpackage, _) =
        env.resolve_with_context("my-subsubpackage", subcontext).await.unwrap();

    subsubpackage.verify_contents(&resolved_subsubpackage).await.unwrap();

    env.stop().await;
}

#[fuchsia::test]
async fn relative_url_empty_context_fails() {
    let env = TestEnvBuilder::new().enable_subpackages().build().await;
    let subpackage = PackageBuilder::new("subpackage")
        .add_resource_at("subpackage-blob", "subpackage-blob-contents".as_bytes())
        .build()
        .await
        .unwrap();
    let superpackage = PackageBuilder::new("superpackage")
        .add_subpackage("my-subpackage", &subpackage)
        .build()
        .await
        .unwrap();
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&superpackage)
            .add_package(&subpackage)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = Arc::clone(&repo).server().start().unwrap();
    let repo_config = served_repository.make_repo_config("fuchsia-pkg://test".parse().unwrap());
    let () = env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();
    let (_, _) = env
        .resolve_package("fuchsia-pkg://test/superpackage")
        .await
        .expect("package to resolve without error");

    assert_matches!(
        env.resolve_with_context("my-subpackage", pkg::ResolutionContext::new()).await,
        Err(fpkg::ResolveError::InvalidContext)
    );

    env.stop().await;
}

#[fuchsia::test]
async fn bad_relative_url_fails() {
    let env = TestEnvBuilder::new().enable_subpackages().build().await;
    let subpackage = PackageBuilder::new("subpackage")
        .add_resource_at("subpackage-blob", "subpackage-blob-contents".as_bytes())
        .build()
        .await
        .unwrap();
    let superpackage = PackageBuilder::new("superpackage")
        .add_subpackage("my-subpackage", &subpackage)
        .build()
        .await
        .unwrap();
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&superpackage)
            .add_package(&subpackage)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = Arc::clone(&repo).server().start().unwrap();
    let repo_config = served_repository.make_repo_config("fuchsia-pkg://test".parse().unwrap());
    let () = env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();
    let (_, context) = env
        .resolve_package("fuchsia-pkg://test/superpackage")
        .await
        .expect("package to resolve without error");

    assert_matches!(
        // Uses the name of the package as identified by the package's own meta.far and the TUF
        // metadata, instead of as identified by the superpackage manifest. This should not work.
        env.resolve_with_context("subpackage", context).await,
        Err(fpkg::ResolveError::PackageNotFound)
    );

    env.stop().await;
}

#[fuchsia::test]
async fn base_superpackage_base_subpackage_succeeds() {
    let subpackage = PackageBuilder::new("subpackage")
        .add_resource_at("subpackage-blob", "subpackage-blob-contents".as_bytes())
        .build()
        .await
        .unwrap();
    let superpackage = PackageBuilder::new("superpackage")
        .add_subpackage("my-subpackage", &subpackage)
        .build()
        .await
        .unwrap();
    let system_image_package =
        SystemImageBuilder::new().static_packages(&[&superpackage, &subpackage]).build().await;
    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&superpackage, &subpackage])
        .enable_subpackages()
        .build()
        .await;
    let (_, context) = env
        .resolve_package("fuchsia-pkg://fuchsia.com/superpackage")
        .await
        .expect("package to resolve without error");

    let (resolved_subpackage, _) =
        env.resolve_with_context("my-subpackage", context).await.unwrap();

    subpackage.verify_contents(&resolved_subpackage).await.unwrap();

    env.stop().await;
}

#[fuchsia::test]
async fn base_superpackage_non_base_subpackage_fails() {
    let subpackage = PackageBuilder::new("subpackage")
        .add_resource_at("subpackage-blob", "subpackage-blob-contents".as_bytes())
        .build()
        .await
        .unwrap();
    let superpackage = PackageBuilder::new("superpackage")
        .add_subpackage("my-subpackage", &subpackage)
        .build()
        .await
        .unwrap();
    let system_image_package =
        SystemImageBuilder::new().static_packages(&[&superpackage]).build().await;
    let env = TestEnvBuilder::new()
        // Still write the subpackage to blobfs, resolve should fail due to validation, not because
        // the blobs are missing.
        .system_image_and_extra_packages(&system_image_package, &[&superpackage, &subpackage])
        .enable_subpackages()
        .build()
        .await;
    let (_, context) = env
        .resolve_package("fuchsia-pkg://fuchsia.com/superpackage")
        .await
        .expect("package to resolve without error");

    assert_matches!(
        env.resolve_with_context("my-subpackage", context).await,
        Err(fpkg::ResolveError::Internal)
    );

    env.stop().await;
}

#[fuchsia::test]
async fn non_base_superpackage_base_subpackage_succeeds() {
    let subpackage = PackageBuilder::new("subpackage")
        .add_resource_at("subpackage-blob", "subpackage-blob-contents".as_bytes())
        .build()
        .await
        .unwrap();
    let superpackage = PackageBuilder::new("superpackage")
        .add_subpackage("my-subpackage", &subpackage)
        .build()
        .await
        .unwrap();
    let system_image_package =
        SystemImageBuilder::new().static_packages(&[&subpackage]).build().await;
    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&subpackage])
        .enable_subpackages()
        .build()
        .await;
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&superpackage)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = Arc::clone(&repo).server().start().unwrap();
    let repo_config = served_repository.make_repo_config("fuchsia-pkg://test".parse().unwrap());
    let () = env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();
    let (_, context) = env
        .resolve_package("fuchsia-pkg://test/superpackage")
        .await
        .expect("package to resolve without error");

    let (resolved_subpackage, _) =
        env.resolve_with_context("my-subpackage", context).await.unwrap();

    subpackage.verify_contents(&resolved_subpackage).await.unwrap();

    env.stop().await;
}
