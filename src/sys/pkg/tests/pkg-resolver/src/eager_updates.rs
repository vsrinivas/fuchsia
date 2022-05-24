// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests eager packages.
use {
    assert_matches::assert_matches,
    fidl_fuchsia_pkg::{GetInfoError, ResolveError},
    fuchsia_async as fasync,
    fuchsia_pkg_testing::{PackageBuilder, RepositoryBuilder, SystemImageBuilder},
    fuchsia_url::pkg_url::PkgUrl,
    lib::{
        get_cup_response_with_name, make_pkg_with_extra_blobs, pkgfs_with_system_image_and_pkg,
        CupDataForTest, MountsBuilder, TestEnvBuilder, EMPTY_REPO_PATH,
    },
    std::sync::Arc,
};

fn make_public_keys_for_test() -> serde_json::Value {
    serde_json::json!({
        "latest": {
            "id": 123,
            "key": "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEHKz/tV8vLO/YnYnrN0smgRUkUoAt\n7qCZFgaBN9g5z3/EgaREkjBNfvZqwRe+/oOo0I8VXytS+fYY3URwKQSODw==\n-----END PUBLIC KEY-----"
        },
        "historical": []
    })
}

#[fasync::run_singlethreaded(test)]
async fn test_empty_eager_config() {
    let package_name = "test-package";
    let pkg = PackageBuilder::new(package_name)
        .add_resource_at("test_file", "test-file-content".as_bytes())
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

    let eager_config = serde_json::json!({
        "packages": [],
    });

    let env = TestEnvBuilder::new()
        .mounts(
            MountsBuilder::new()
                .custom_config_data("eager_package_config.json", eager_config.to_string())
                .enable_dynamic_config(lib::EnableDynamicConfig {
                    enable_dynamic_configuration: true,
                })
                .build(),
        )
        .build()
        .await;

    let served_repository = Arc::clone(&repo).server().start().unwrap();

    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);

    let () = env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    let package = env
        .resolve_package(format!("fuchsia-pkg://test/{}", package_name).as_str())
        .await
        .expect("package to resolve without error");

    // Verify the served package directory contains the exact expected contents.
    pkg.verify_contents(&package).await.unwrap();

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn test_eager_resolve_package() {
    let pkg_name = "test-package";
    let pkg = make_pkg_with_extra_blobs(&pkg_name, 0).await;
    let url = format!("fuchsia-pkg://example.com/{}?hash={}", pkg_name, pkg.meta_far_merkle_root());
    let pkg_url = PkgUrl::parse(&url).unwrap();
    let url_no_hash = pkg_url.strip_hash().to_string();

    let eager_config = serde_json::json!({
        "packages": [
            {
                "url": url_no_hash,
                "public_keys": make_public_keys_for_test(),
            }
        ]
    });

    let system_image_package = SystemImageBuilder::new().build().await;
    let pkgfs = pkgfs_with_system_image_and_pkg(&system_image_package, Some(&pkg)).await;

    let env = TestEnvBuilder::new()
        .pkgfs(pkgfs)
        .mounts(
            MountsBuilder::new()
                .eager_packages(vec![url])
                .custom_config_data("eager_package_config.json", eager_config.to_string())
                .enable_dynamic_config(lib::EnableDynamicConfig {
                    enable_dynamic_configuration: true,
                })
                .build(),
        )
        .build()
        .await;

    let package = env
        .resolve_package(format!("fuchsia-pkg://example.com/{}", pkg_name).as_str())
        .await
        .expect("package to resolve without error");

    // Verify the served package directory contains the exact expected contents.
    pkg.verify_contents(&package).await.unwrap();

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn test_eager_get_hash() {
    let pkg_name = "test-package";
    let pkg = make_pkg_with_extra_blobs(&pkg_name, 0).await;
    let url = format!("fuchsia-pkg://example.com/{}?hash={}", pkg_name, pkg.meta_far_merkle_root());
    let pkg_url = PkgUrl::parse(&url).unwrap();
    let url_no_hash = pkg_url.strip_hash().to_string();

    let eager_config = serde_json::json!({
        "packages": [
            {
                "url": url_no_hash,
                "public_keys": make_public_keys_for_test(),
            }
        ],
    });

    let system_image_package = SystemImageBuilder::new().build().await;
    let pkgfs = pkgfs_with_system_image_and_pkg(&system_image_package, Some(&pkg)).await;

    let env = TestEnvBuilder::new()
        .pkgfs(pkgfs)
        .mounts(
            MountsBuilder::new()
                .eager_packages(vec![url])
                .custom_config_data("eager_package_config.json", eager_config.to_string())
                .enable_dynamic_config(lib::EnableDynamicConfig {
                    enable_dynamic_configuration: true,
                })
                .build(),
        )
        .build()
        .await;

    let package = env.get_hash("fuchsia-pkg://example.com/test-package").await;

    assert_eq!(package.unwrap(), pkg.meta_far_merkle_root().clone().into());

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn test_cup_write() {
    let pkg_name = "test-package";
    let pkg = make_pkg_with_extra_blobs(&pkg_name, 0).await;
    let url = format!("fuchsia-pkg://example.com/{}?hash={}", pkg_name, pkg.meta_far_merkle_root());
    let pkg_url = PkgUrl::parse(&url).unwrap();
    let url_no_hash = pkg_url.strip_hash().to_string();

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = Arc::clone(&repo).server().start().unwrap();

    let repo_config = served_repository.make_repo_config(pkg_url.repo().clone());

    let eager_config = serde_json::json!({
        "packages": [
            {
                "url": url_no_hash,
                "public_keys": make_public_keys_for_test(),
            }
        ]
    });

    let env = TestEnvBuilder::new()
        .mounts(
            MountsBuilder::new()
                .static_repository(repo_config)
                .custom_config_data("eager_package_config.json", eager_config.to_string())
                .enable_dynamic_config(lib::EnableDynamicConfig {
                    enable_dynamic_configuration: true,
                })
                .build(),
        )
        .build()
        .await;

    // can't get info or resolve before write
    assert_matches!(env.cup_get_info(&url_no_hash).await, Err(GetInfoError::NotAvailable));
    assert_matches!(env.resolve_package(&url_no_hash).await, Err(ResolveError::PackageNotFound));

    env.cup_write(
        &url,
        CupDataForTest::builder().response(get_cup_response_with_name(&pkg_url)).build().into(),
    )
    .await
    .unwrap();

    // now get info and resolve works
    let (version, channel) = env.cup_get_info(&url_no_hash).await.unwrap();
    assert_eq!(version, "1.2.3.4");
    assert_eq!(channel, "stable");

    let package = env.resolve_package(&url_no_hash).await.unwrap();
    // Verify the served package directory contains the exact expected contents.
    pkg.verify_contents(&package).await.unwrap();

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn test_cup_get_info_persisted() {
    let pkg_name = "test-package";
    let pkg = make_pkg_with_extra_blobs(&pkg_name, 0).await;
    let url = format!("fuchsia-pkg://example.com/{}?hash={}", pkg_name, pkg.meta_far_merkle_root());
    let pkg_url = PkgUrl::parse(&url).unwrap();
    let url_no_hash = pkg_url.strip_hash().to_string();

    let eager_config = serde_json::json!({
        "packages": [
            {
                "url": url_no_hash ,
                "public_keys": make_public_keys_for_test(),
            }
        ],
    });

    let system_image_package = SystemImageBuilder::new().build().await;
    let pkgfs = pkgfs_with_system_image_and_pkg(&system_image_package, Some(&pkg)).await;

    let env = TestEnvBuilder::new()
        .pkgfs(pkgfs)
        .mounts(
            MountsBuilder::new()
                .eager_packages(vec![url])
                .custom_config_data("eager_package_config.json", eager_config.to_string())
                .build(),
        )
        .build()
        .await;

    let (version, channel) = env.cup_get_info(&url_no_hash).await.unwrap();

    assert_eq!(version, "1.2.3.4");
    assert_eq!(channel, "stable");

    env.stop().await;
}
