// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests eager packages.
use {
    assert_matches::assert_matches,
    cobalt_sw_delivery_registry as metrics, fidl_fuchsia_pkg as fpkg,
    fidl_fuchsia_pkg::{GetInfoError, ResolveError},
    fidl_fuchsia_pkg_ext::CupData,
    fuchsia_async as fasync,
    fuchsia_pkg_testing::{
        serve::responder, PackageBuilder, RepositoryBuilder, SystemImageBuilder,
    },
    fuchsia_url::PinnedAbsolutePackageUrl,
    lib::{
        get_cup_response_with_name, make_pkg_with_extra_blobs, MountsBuilder, TestEnvBuilder,
        EMPTY_REPO_PATH,
    },
    omaha_client::{
        cup_ecdsa::{
            test_support::{
                make_default_json_public_keys_for_test, make_default_public_key_id_for_test,
                make_expected_signature_for_test, make_keys_for_test, make_public_keys_for_test,
                make_standard_intermediate_for_test,
            },
            Cupv2RequestHandler, PublicKeyId, StandardCupv2Handler,
        },
        protocol::request::Request,
    },
    std::sync::Arc,
};

fn make_cup_data(cup_response: &[u8]) -> CupData {
    let (priv_key, public_key) = make_keys_for_test();
    let public_key_id: PublicKeyId = make_default_public_key_id_for_test();
    let public_keys = make_public_keys_for_test(public_key_id, public_key);
    let cup_handler = StandardCupv2Handler::new(&public_keys);
    let request = Request::default();
    let mut intermediate = make_standard_intermediate_for_test(request);
    let request_metadata = cup_handler.decorate_request(&mut intermediate).unwrap();
    let request_body = intermediate.serialize_body().unwrap();
    let expected_signature: Vec<u8> =
        make_expected_signature_for_test(&priv_key, &request_metadata, &cup_response);
    fidl_fuchsia_pkg_ext::CupData::builder()
        .key_id(public_key_id)
        .nonce(request_metadata.nonce)
        .request(request_body)
        .response(cup_response)
        .signature(expected_signature)
        .build()
}

#[fuchsia::test]
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

    let (package, _resolved_context) = env
        .resolve_package(format!("fuchsia-pkg://test/{}", package_name).as_str())
        .await
        .expect("package to resolve without error");

    // Verify the served package directory contains the exact expected contents.
    pkg.verify_contents(&package).await.unwrap();

    env.stop().await;
}

#[fuchsia::test]
async fn test_eager_resolve_package() {
    let pkg_name = "test-package";
    let pkg = make_pkg_with_extra_blobs(&pkg_name, 0).await;
    let pkg_url = PinnedAbsolutePackageUrl::parse(&format!(
        "fuchsia-pkg://example.com/{}?hash={}",
        pkg_name,
        pkg.meta_far_merkle_root()
    ))
    .unwrap();

    let eager_config = serde_json::json!({
        "packages": [
            {
                "url": pkg_url.as_unpinned(),
                "public_keys": make_default_json_public_keys_for_test(),
                "minimum_required_version": "1.2.3.4",
            }
        ]
    });

    let system_image_package = SystemImageBuilder::new().build().await;

    let cup_response = get_cup_response_with_name(&pkg_url);
    let cup_data: CupData = make_cup_data(&cup_response);

    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&pkg])
        .mounts(
            MountsBuilder::new()
                .eager_packages(vec![(pkg_url.clone(), cup_data.clone())])
                .custom_config_data("eager_package_config.json", eager_config.to_string())
                .enable_dynamic_config(lib::EnableDynamicConfig {
                    enable_dynamic_configuration: true,
                })
                .build(),
        )
        .build()
        .await;

    let (package, _resolved_context) = env
        .resolve_package(format!("fuchsia-pkg://example.com/{}", pkg_name).as_str())
        .await
        .expect("package to resolve without error");

    // Verify the served package directory contains the exact expected contents.
    pkg.verify_contents(&package).await.unwrap();

    env.stop().await;
}

#[fuchsia::test]
async fn test_eager_resolve_package_while_updating() {
    let pkg_name = "test-package";
    let pkg = make_pkg_with_extra_blobs(&pkg_name, 0).await;
    let new_pkg = make_pkg_with_extra_blobs(&pkg_name, 1).await;
    let pkg_url = PinnedAbsolutePackageUrl::parse(&format!(
        "fuchsia-pkg://example.com/{}?hash={}",
        pkg_name,
        pkg.meta_far_merkle_root()
    ))
    .unwrap();

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&new_pkg)
            .build()
            .await
            .unwrap(),
    );
    let (blocking_responder, unblocking_closure_receiver) = responder::BlockResponseBodyOnce::new();
    let blocking_responder = responder::ForPath::new("/timestamp.json", blocking_responder);

    let served_repository =
        Arc::clone(&repo).server().response_overrider(blocking_responder).start().unwrap();

    let repo_config = served_repository.make_repo_config(pkg_url.repository().clone());

    let unpinned_pkg_url = pkg_url.as_unpinned().to_string();
    let eager_config = serde_json::json!({
        "packages": [
            {
                "url": unpinned_pkg_url,
                "public_keys": make_default_json_public_keys_for_test(),
                "minimum_required_version": "1.2.3.4",
            }
        ]
    });

    let system_image_package = SystemImageBuilder::new().build().await;

    let cup_response = get_cup_response_with_name(&pkg_url);
    let cup_data: CupData = make_cup_data(&cup_response);

    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&pkg])
        .mounts(
            MountsBuilder::new()
                .static_repository(repo_config)
                .eager_packages(vec![(pkg_url, cup_data)])
                .custom_config_data("eager_package_config.json", eager_config.to_string())
                .enable_dynamic_config(lib::EnableDynamicConfig {
                    enable_dynamic_configuration: true,
                })
                .build(),
        )
        .build()
        .await;

    let new_pkg_url = PinnedAbsolutePackageUrl::parse(&format!(
        "fuchsia-pkg://example.com/{}?hash={}",
        pkg_name,
        new_pkg.meta_far_merkle_root()
    ))
    .unwrap();
    let new_cup_response = get_cup_response_with_name(&new_pkg_url);
    let new_cup_data: CupData = make_cup_data(&new_cup_response);

    // spawn a cup write request, which will be blocked.
    let cup_proxy = env.proxies.cup.clone();
    let cup_write_task = fasync::Task::spawn(async move {
        cup_proxy
            .write(&mut fpkg::PackageUrl { url: new_pkg_url.to_string() }, new_cup_data.into())
            .await
            .unwrap()
            .unwrap()
    });

    let unblock_server =
        unblocking_closure_receiver.await.expect("received unblocking future from hyper server");

    // resolve package isn't blocked by ongoing cup write.
    let (package, _resolved_context) =
        env.resolve_package(&unpinned_pkg_url).await.expect("package to resolve without error");
    // this is still the old package.
    pkg.verify_contents(&package).await.unwrap();

    unblock_server();
    cup_write_task.await;

    // resolve again it should be the new package.
    let (package, _resolved_context) =
        env.resolve_package(&unpinned_pkg_url).await.expect("package to resolve without error");
    new_pkg.verify_contents(&package).await.unwrap();

    env.stop().await;
}

#[fuchsia::test]
async fn test_eager_get_hash() {
    let pkg_name = "test-package";
    let pkg = make_pkg_with_extra_blobs(&pkg_name, 0).await;
    let pkg_url = PinnedAbsolutePackageUrl::parse(&format!(
        "fuchsia-pkg://example.com/{}?hash={}",
        pkg_name,
        pkg.meta_far_merkle_root()
    ))
    .unwrap();

    let eager_config = serde_json::json!({
        "packages": [
            {
                "url": pkg_url.as_unpinned(),
                "public_keys": make_default_json_public_keys_for_test(),
                "minimum_required_version": "1.2.3.4",
            }
        ],
    });

    let system_image_package = SystemImageBuilder::new().build().await;

    let cup_response = get_cup_response_with_name(&pkg_url);
    let cup_data: CupData = make_cup_data(&cup_response);

    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&pkg])
        .mounts(
            MountsBuilder::new()
                .eager_packages(vec![(pkg_url.clone(), cup_data.clone())])
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

#[fuchsia::test]
async fn test_cup_write() {
    let pkg_name = "test-package";
    let pkg = make_pkg_with_extra_blobs(&pkg_name, 0).await;
    let pkg_url = PinnedAbsolutePackageUrl::parse(&format!(
        "fuchsia-pkg://example.com/{}?hash={}",
        pkg_name,
        pkg.meta_far_merkle_root()
    ))
    .unwrap();

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = Arc::clone(&repo).server().start().unwrap();

    let repo_config = served_repository.make_repo_config(pkg_url.repository().clone());

    let eager_config = serde_json::json!({
        "packages": [
            {
                "url": pkg_url.as_unpinned(),
                "public_keys": make_default_json_public_keys_for_test(),
                "minimum_required_version": "1.2.3.4",
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

    env.assert_count_events(
        metrics::LOAD_PERSISTENT_EAGER_PACKAGE_METRIC_ID,
        vec![(metrics::LoadPersistentEagerPackageMetricDimensionResult::NotAvailable, 0)],
    )
    .await;

    // can't get info or resolve before write
    assert_matches!(
        env.cup_get_info(pkg_url.as_unpinned().to_string()).await,
        Err(GetInfoError::NotAvailable)
    );
    assert_matches!(
        env.resolve_package(&pkg_url.as_unpinned().to_string()).await,
        Err(ResolveError::PackageNotFound)
    );

    let cup_response = get_cup_response_with_name(&pkg_url);
    let cup_data: CupData = make_cup_data(&cup_response);
    env.cup_write(pkg_url.to_string(), cup_data).await.unwrap();

    // now get info and resolve works
    let (version, channel) = env.cup_get_info(pkg_url.as_unpinned().to_string()).await.unwrap();
    assert_eq!(version, "1.2.3.4");
    assert_eq!(channel, "stable");

    let (package, _resolved_context) =
        env.resolve_package(&pkg_url.as_unpinned().to_string()).await.unwrap();
    // Verify the served package directory contains the exact expected contents.
    pkg.verify_contents(&package).await.unwrap();

    env.assert_count_events(
        metrics::CUP_GETINFO_METRIC_ID,
        vec![
            metrics::CupGetinfoMetricDimensionResult::NotAvailable,
            metrics::CupGetinfoMetricDimensionResult::Success,
        ],
    )
    .await;

    env.assert_count_events(
        metrics::CUP_WRITE_METRIC_ID,
        vec![metrics::CupGetinfoMetricDimensionResult::Success],
    )
    .await;

    env.stop().await;
}

#[fuchsia::test]
async fn test_cup_get_info_persisted() {
    let pkg_name = "test-package";
    let pkg = make_pkg_with_extra_blobs(&pkg_name, 0).await;
    let pkg_url = PinnedAbsolutePackageUrl::parse(&format!(
        "fuchsia-pkg://example.com/{}?hash={}",
        pkg_name,
        pkg.meta_far_merkle_root()
    ))
    .unwrap();

    let eager_config = serde_json::json!({
        "packages": [
            {
                "url": pkg_url.as_unpinned() ,
                "public_keys": make_default_json_public_keys_for_test(),
                "minimum_required_version": "1.2.3.4",
            }
        ],
    });

    let system_image_package = SystemImageBuilder::new().build().await;

    let cup_response = get_cup_response_with_name(&pkg_url);
    let cup_data: CupData = make_cup_data(&cup_response);

    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&pkg])
        .mounts(
            MountsBuilder::new()
                .eager_packages(vec![(pkg_url.clone(), cup_data.clone())])
                .custom_config_data("eager_package_config.json", eager_config.to_string())
                .build(),
        )
        .build()
        .await;

    env.assert_count_events(
        metrics::LOAD_PERSISTENT_EAGER_PACKAGE_METRIC_ID,
        vec![(metrics::LoadPersistentEagerPackageMetricDimensionResult::Success, 0)],
    )
    .await;

    let (version, channel) = env.cup_get_info(&pkg_url.as_unpinned().to_string()).await.unwrap();

    assert_eq!(version, "1.2.3.4");
    assert_eq!(channel, "stable");

    env.assert_count_events(
        metrics::CUP_GETINFO_METRIC_ID,
        vec![metrics::CupGetinfoMetricDimensionResult::Success],
    )
    .await;

    env.stop().await;
}
