// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    fuchsia_pkg_testing::{Package, PackageBuilder, RepositoryBuilder, SystemImageBuilder},
    lib::{
        EnableDynamicConfig, MountsBuilder, PersistedReposConfig, TestEnvBuilder, EMPTY_REPO_PATH,
    },
    std::{convert::TryInto, sync::Arc},
};

async fn test_package(name: &str, contents: &str) -> Package {
    PackageBuilder::new(name)
        .add_resource_at("p/t/o", format!("contents: {}\n", contents).as_bytes())
        .build()
        .await
        .expect("build package")
}

// When a persisted repository configuration is present, and a dynamic repo is configured to
// persist metadata, this test validates that pkg-resolver can resolve a previously fetched package
// across a restart.
#[fuchsia::test]
async fn test_resolve_persisted_package_succeeds() {
    let pkg_name = "test_resolve_persisted_package_succeeds";

    let cache_pkg = test_package(pkg_name, "cache").await;
    let system_image_package =
        SystemImageBuilder::new().cache_packages(&[&cache_pkg]).build().await;

    let mut env = TestEnvBuilder::new()
        .mounts(
            MountsBuilder::new()
                .enable_dynamic_config(EnableDynamicConfig { enable_dynamic_configuration: true })
                .persisted_repos_config(PersistedReposConfig {
                    persisted_repos_dir: "repos".to_string(),
                })
                .build(),
        )
        .system_image_and_extra_packages(&system_image_package, &[&cache_pkg])
        .build()
        .await;

    // Resolve the package and ensure it's the cached version.
    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", pkg_name);
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    cache_pkg.verify_contents(&package_dir).await.expect("to resolve the cached version");

    // Put a copy of the package with altered contents in the repo to make sure
    // we're getting the one from the cache.
    let repo_pkg = test_package(pkg_name, "repo").await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&system_image_package)
            .add_package(&repo_pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo.server().start().unwrap();
    let repo_config = served_repository
        .make_repo_config_with_persistent_storage("fuchsia-pkg://fuchsia.com".try_into().unwrap());
    // System cache fallback is only triggered for fuchsia.com repos.
    let () = env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    // Try to resolve again, make sure we see the new version.
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    repo_pkg.verify_contents(&package_dir).await.expect("to resolve the new version");
    assert!(cache_pkg.verify_contents(&package_dir).await.is_err());

    served_repository.stop().await;
    env.restart_pkg_resolver().await;

    // Try a final resolve. If the persisted repo config works, this must resolve the second
    // version of the package that we persisted from when the repo was live.
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    repo_pkg.verify_contents(&package_dir).await.expect("to resolve the new version post-restart");
    assert!(cache_pkg.verify_contents(&package_dir).await.is_err());

    env.stop().await;
}

// When a persisted repository configuration is present, but lists an empty string, pkg-resolver
// should not persist TUF metadata.
#[fuchsia::test]
async fn test_resolve_empty_config_fails() {
    let pkg_name = "test_resolve_empty_config_fails";

    let cache_pkg = test_package(pkg_name, "cache").await;
    let system_image_package =
        SystemImageBuilder::new().cache_packages(&[&cache_pkg]).build().await;

    let mut env = TestEnvBuilder::new()
        .mounts(
            MountsBuilder::new()
                .enable_dynamic_config(EnableDynamicConfig { enable_dynamic_configuration: true })
                .persisted_repos_config(PersistedReposConfig {
                    persisted_repos_dir: "".to_string(),
                })
                .build(),
        )
        .system_image_and_extra_packages(&system_image_package, &[&cache_pkg])
        .build()
        .await;

    // Resolve the package and ensure it's the cached version.
    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", pkg_name);
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    cache_pkg.verify_contents(&package_dir).await.expect("to resolve the cached version");

    // Put a copy of the package with altered contents in the repo to make sure
    // we're getting the one from the cache.
    let repo_pkg = test_package(pkg_name, "repo").await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&system_image_package)
            .add_package(&repo_pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo.server().start().unwrap();
    let repo_config = served_repository
        .make_repo_config_with_persistent_storage("fuchsia-pkg://fuchsia.com".try_into().unwrap());
    // System cache fallback is only triggered for fuchsia.com repos.
    let () = env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    // Try to resolve again. We expect to resolve the cached version because the pkg-resolver
    // configuration does not allow us to instantiate a persisted repository.
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    cache_pkg.verify_contents(&package_dir).await.expect("to resolve the cached version again");
    assert!(repo_pkg.verify_contents(&package_dir).await.is_err());

    served_repository.stop().await;
    env.restart_pkg_resolver().await;

    // Try a final resolve. Because the config lists an empty string, this should result in
    // resolving to the cache_pkg.
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    cache_pkg.verify_contents(&package_dir).await.expect("to finally resolve the cached version");
    assert!(repo_pkg.verify_contents(&package_dir).await.is_err());

    env.stop().await;
}

// When a persisted repo config is present, but dynamic repository configurations are disabled, we
// should not persist the TUF metadata.
#[fuchsia::test]
async fn test_resolve_dynamic_disabled_fails() {
    let pkg_name = "test_resolve_dynamic_disabled_fails";

    let cache_pkg = test_package(pkg_name, "cache").await;
    let system_image_package =
        SystemImageBuilder::new().cache_packages(&[&cache_pkg]).build().await;
    let mut env = TestEnvBuilder::new()
        .mounts(
            MountsBuilder::new()
                .enable_dynamic_config(EnableDynamicConfig { enable_dynamic_configuration: false })
                .persisted_repos_config(PersistedReposConfig {
                    persisted_repos_dir: "repos".to_string(),
                })
                .build(),
        )
        .system_image_and_extra_packages(&system_image_package, &[&cache_pkg])
        .build()
        .await;

    // Resolve the package and ensure it's the cached version.
    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", pkg_name);
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    cache_pkg.verify_contents(&package_dir).await.expect("to resolve the cached version");

    // Put a copy of the package with altered contents in the repo to make sure
    // we're getting the one from the cache.
    let repo_pkg = test_package(pkg_name, "repo").await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&system_image_package)
            .add_package(&repo_pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo.server().start().unwrap();
    let repo_config = served_repository
        .make_repo_config_with_persistent_storage("fuchsia-pkg://fuchsia.com".try_into().unwrap());
    // System cache fallback is only triggered for fuchsia.com repos. This will fail because
    // dynamic repository configuration is itself disallowed in this configuration.
    let _ = env
        .proxies
        .repo_manager
        .add(repo_config.into())
        .await
        .unwrap()
        .expect_err("can't set repo config");

    // Because dynamic repositories cannot be created in this pkg-resolver configuration, we were
    // unable to install the repository configuration to persist the repo, and we expect to resolve
    // the cached package.
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    cache_pkg.verify_contents(&package_dir).await.expect("to resolve the cached version again");
    assert!(repo_pkg.verify_contents(&package_dir).await.is_err());

    served_repository.stop().await;
    env.restart_pkg_resolver().await;

    // Try a final resolve. This will again return the cached version.
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    cache_pkg.verify_contents(&package_dir).await.expect("to finally resolve the cached version");
    assert!(repo_pkg.verify_contents(&package_dir).await.is_err());

    env.stop().await;
}

// When a persisted repository configuration is not present, we should not persist TUF metadata.
#[fuchsia::test]
async fn test_resolve_no_config_fails() {
    let pkg_name = "test_resolve_no_config_fails";

    let cache_pkg = test_package(pkg_name, "cache").await;
    let system_image_package =
        SystemImageBuilder::new().cache_packages(&[&cache_pkg]).build().await;

    let mut env = TestEnvBuilder::new()
        .mounts(
            MountsBuilder::new()
                .enable_dynamic_config(EnableDynamicConfig { enable_dynamic_configuration: true })
                .build(),
        )
        .system_image_and_extra_packages(&system_image_package, &[&cache_pkg])
        .build()
        .await;

    // Resolve the package and ensure it's the cached version.
    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", pkg_name);
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    cache_pkg.verify_contents(&package_dir).await.expect("to resolve the cached version");

    // Put a copy of the package with altered contents in the repo to make sure
    // we're getting the one from the cache.
    let repo_pkg = test_package(pkg_name, "repo").await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&system_image_package)
            .add_package(&repo_pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo.server().start().unwrap();
    let repo_config = served_repository
        .make_repo_config_with_persistent_storage("fuchsia-pkg://fuchsia.com".try_into().unwrap());
    // System cache fallback is only triggered for fuchsia.com repos.
    let () = env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    // Try to resolve again. We expect to resolve the cached version because the pkg-resolver
    // configuration does not allow us to instantiate a persisted repository.
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    cache_pkg.verify_contents(&package_dir).await.expect("to resolve the cached version again");
    assert!(repo_pkg.verify_contents(&package_dir).await.is_err());

    served_repository.stop().await;
    env.restart_pkg_resolver().await;

    // Try a final resolve. Because the config lists an empty string, this should result in
    // resolving to the cache_pkg.
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    cache_pkg.verify_contents(&package_dir).await.expect("to finally resolve the cached version");
    assert!(repo_pkg.verify_contents(&package_dir).await.is_err());

    env.stop().await;
}
