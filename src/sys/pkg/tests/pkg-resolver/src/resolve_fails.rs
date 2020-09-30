// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    fidl_fuchsia_pkg_ext::RepositoryConfigBuilder,
    fuchsia_async as fasync,
    fuchsia_pkg_testing::{PackageBuilder, RepositoryBuilder},
    fuchsia_zircon::Status,
    lib::{TestEnvBuilder, EMPTY_REPO_PATH},
    std::sync::Arc,
};

#[fasync::run_singlethreaded(test)]
async fn resolve_disallow_local_mirror_fails() {
    let pkg = PackageBuilder::new("test")
        .add_resource_at("test_file", "hi there".as_bytes())
        .build()
        .await
        .unwrap();

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );

    // Local mirror defaults not being enabled.
    let env = TestEnvBuilder::new()
        .local_mirror_repo(&repo, "fuchsia-pkg://test".parse().unwrap())
        .build()
        .await;
    let repo_config = repo.make_repo_config("fuchsia-pkg://test".parse().unwrap(), None, true);
    env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());
    let result = env.resolve_package(&pkg_url).await;

    assert_eq!(result.unwrap_err(), Status::INTERNAL);
}

#[fasync::run_singlethreaded(test)]
async fn resolve_local_and_remote_mirrors_fails() {
    let pkg = PackageBuilder::new("test")
        .add_resource_at("test_file", "hi there".as_bytes())
        .build()
        .await
        .unwrap();

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );

    let env = TestEnvBuilder::new()
        .allow_local_mirror()
        .local_mirror_repo(&repo, "fuchsia-pkg://test".parse().unwrap())
        .build()
        .await;
    let server = repo.server().start().expect("Starting server succeeds");
    let repo_config = server.make_repo_config("fuchsia-pkg://test".parse().unwrap());
    let repo_config = RepositoryConfigBuilder::from(repo_config).use_local_mirror(true).build();
    env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());
    let result = env.resolve_package(&pkg_url).await;

    assert_eq!(result.unwrap_err(), Status::INTERNAL);
}
