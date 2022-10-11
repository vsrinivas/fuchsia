// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    assert_matches::assert_matches,
    fidl_fuchsia_pkg as fpkg,
    fuchsia_pkg_testing::{PackageBuilder, RepositoryBuilder},
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
        env.resolve_with_context("fuchsia-pkg://test/package-name", vec![0u8].into()).await,
        Err(fpkg::ResolveError::InvalidContext)
    );

    // Succeeds with empty context
    let (resolved_pkg, _) =
        env.resolve_with_context("fuchsia-pkg://test/package-name", vec![].into()).await.unwrap();
    pkg.verify_contents(&resolved_pkg).await.unwrap();

    env.stop().await;
}

#[fuchsia::test]
async fn relative_url_not_implemented() {
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

    assert_matches!(
        env.resolve_with_context("package-name", vec![0u8].into()).await,
        Err(fpkg::ResolveError::Internal)
    );

    env.stop().await;
}
