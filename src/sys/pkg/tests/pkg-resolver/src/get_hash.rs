// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests the fuchsia.pkg.PackageResolver.GetHash FIDL method
use {
    fuchsia_async as fasync,
    fuchsia_pkg_testing::RepositoryBuilder,
    fuchsia_zircon::Status,
    lib::{make_pkg_with_extra_blobs, TestEnvBuilder, EMPTY_REPO_PATH},
    matches::assert_matches,
    std::sync::Arc,
};

#[fasync::run_singlethreaded(test)]
async fn succeeds_if_package_present() {
    let env = TestEnvBuilder::new().build().await;
    let pkg_name = "a-fake-pkg-name";
    let pkg = make_pkg_with_extra_blobs(pkg_name, 0).await;
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = Arc::clone(&repo).server().start().unwrap();
    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);
    let () = env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    let package = env.get_hash("fuchsia-pkg://test/a-fake-pkg-name").await;

    assert_eq!(package.unwrap(), pkg.meta_far_merkle_root().clone().into());

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn fails_if_package_absent() {
    let env = TestEnvBuilder::new().build().await;
    let repo =
        Arc::new(RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH).build().await.unwrap());
    let served_repository = Arc::clone(&repo).server().start().unwrap();
    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);
    let () = env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    let package = env.get_hash("fuchsia-pkg://test/b-fake-pkg-name").await;

    assert_matches!(package, Err(status) if status == Status::NOT_FOUND);

    env.stop().await;
}
