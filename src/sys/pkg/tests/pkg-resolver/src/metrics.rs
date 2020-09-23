// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests the Cobalt metrics reporting.
use {
    cobalt_client::traits::{AsEventCode, AsEventCodes},
    cobalt_sw_delivery_registry as metrics,
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_cobalt::{CobaltEvent, CountEvent, EventPayload},
    fidl_fuchsia_pkg::UpdatePolicy,
    fuchsia_async as fasync,
    fuchsia_pkg_testing::{
        serve::{handler, UriPathHandler},
        Package, PackageBuilder, RepositoryBuilder,
    },
    fuchsia_zircon::Status,
    lib::{make_repo, make_repo_config, MountsBuilder, TestEnv, TestEnvBuilder, EMPTY_REPO_PATH},
    matches::assert_matches,
    serde_json::json,
    std::sync::Arc,
};

async fn assert_count_events(
    env: &TestEnv,
    expected_metric_id: u32,
    expected_event_codes: Vec<impl AsEventCodes>,
) {
    let actual_events = env
        .mocks
        .logger_factory
        .wait_for_at_least_n_events_with_metric_id(expected_event_codes.len(), expected_metric_id)
        .await;
    assert_eq!(
        actual_events.len(),
        expected_event_codes.len(),
        "event count different than expected, actual_events: {:?}",
        actual_events
    );

    for (event, expected_codes) in
        actual_events.into_iter().zip(expected_event_codes.into_iter().map(|c| c.as_event_codes()))
    {
        assert_matches!(
            event,
            CobaltEvent {
                metric_id,
                event_codes,
                component: None,
                payload: EventPayload::EventCount(CountEvent {
                    period_duration_micros: 0,
                    count: 1
                }),
            } if metric_id == expected_metric_id && event_codes == expected_codes
        )
    }
}

async fn assert_elapsed_duration_events(
    env: &TestEnv,
    expected_metric_id: u32,
    expected_event_codes: Vec<impl AsEventCodes>,
) {
    let actual_events = env
        .mocks
        .logger_factory
        .wait_for_at_least_n_events_with_metric_id(expected_event_codes.len(), expected_metric_id)
        .await;
    assert_eq!(
        actual_events.len(),
        expected_event_codes.len(),
        "event count different than expected, actual_events: {:?}",
        actual_events
    );
    for (event, expected_codes) in
        actual_events.into_iter().zip(expected_event_codes.into_iter().map(|c| c.as_event_codes()))
    {
        assert_matches!(
            event,
            CobaltEvent {
                metric_id,
                event_codes,
                component: None,
                payload: EventPayload::ElapsedMicros(_),
            } if metric_id == expected_metric_id && event_codes == expected_codes
        )
    }
}

async fn verify_resolve_emits_cobalt_events_with_metric_id(
    pkg: Package,
    handler: Option<impl UriPathHandler>,
    expected_resolve_result: Result<(), Status>,
    metric_id: u32,
    expected_events: Vec<impl AsEventCode>,
) {
    let env = TestEnvBuilder::new().build().await;
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );
    let mut served_repository = repo.server();
    if let Some(handler) = handler {
        served_repository = served_repository.uri_path_override_handler(handler);
    }
    let served_repository = served_repository.start().unwrap();
    let repo_url = "fuchsia-pkg://example.com".parse().unwrap();
    let config = served_repository.make_repo_config(repo_url);
    let () = env.proxies.repo_manager.add(config.clone().into()).await.unwrap().unwrap();

    assert_eq!(
        env.resolve_package(&format!("fuchsia-pkg://example.com/{}", pkg.name())).await.map(|_| ()),
        expected_resolve_result
    );
    assert_count_events(&env, metric_id, expected_events).await;
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn repository_manager_load_static_configs_success() {
    let env = TestEnvBuilder::new()
        .mounts(lib::MountsBuilder::new().static_repository(make_repo()).build())
        .build()
        .await;

    assert_count_events(
        &env,
        metrics::REPOSITORY_MANAGER_LOAD_STATIC_CONFIGS_METRIC_ID,
        vec![metrics::RepositoryManagerLoadStaticConfigsMetricDimensionResult::Success],
    )
    .await;

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn pkg_resolver_startup_duration() {
    let env = TestEnvBuilder::new().build().await;

    let events = env
        .mocks
        .logger_factory
        .wait_for_at_least_n_events_with_metric_id(
            1,
            metrics::PKG_RESOLVER_STARTUP_DURATION_METRIC_ID,
        )
        .await;
    assert_matches!(
        events[0],
        CobaltEvent {
            metric_id: metrics::PKG_RESOLVER_STARTUP_DURATION_METRIC_ID,
            ref event_codes,
            component: None,
            payload: EventPayload::ElapsedMicros(_)
        } if event_codes == &vec![0]
    );

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn repository_manager_load_static_configs_io() {
    let env = TestEnvBuilder::new().build().await;

    assert_count_events(
        &env,
        metrics::REPOSITORY_MANAGER_LOAD_STATIC_CONFIGS_METRIC_ID,
        vec![metrics::RepositoryManagerLoadStaticConfigsMetricDimensionResult::Io],
    )
    .await;

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn repository_manager_load_static_configs_parse() {
    let env = TestEnvBuilder::new()
        .mounts(
            MountsBuilder::new()
                .custom_config_data("repositories/invalid.json", "invalid-json")
                .build(),
        )
        .build()
        .await;

    assert_count_events(
        &env,
        metrics::REPOSITORY_MANAGER_LOAD_STATIC_CONFIGS_METRIC_ID,
        vec![metrics::RepositoryManagerLoadStaticConfigsMetricDimensionResult::Parse],
    )
    .await;

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn repository_manager_load_static_configs_overridden() {
    let json = serde_json::to_string(&make_repo_config(&make_repo())).unwrap();
    let env = TestEnvBuilder::new()
        .mounts(
            MountsBuilder::new()
                .custom_config_data("repositories/1.json", &json)
                .custom_config_data("repositories/2.json", json)
                .build(),
        )
        .build()
        .await;

    assert_count_events(
        &env,
        metrics::REPOSITORY_MANAGER_LOAD_STATIC_CONFIGS_METRIC_ID,
        vec![metrics::RepositoryManagerLoadStaticConfigsMetricDimensionResult::Overridden],
    )
    .await;

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn resolve_success_regular() {
    let env = TestEnvBuilder::new().build().await;
    let pkg = PackageBuilder::new("just_meta_far").build().await.expect("created pkg");
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo.server();
    let served_repository = served_repository.start().unwrap();
    let repo_url = "fuchsia-pkg://example.com".parse().unwrap();
    let config = served_repository.make_repo_config(repo_url);
    let () = env.proxies.repo_manager.add(config.clone().into()).await.unwrap().unwrap();

    assert_eq!(
        env.resolve_package(&format!("fuchsia-pkg://example.com/{}", pkg.name())).await.map(|_| ()),
        Ok(())
    );

    assert_count_events(
        &env,
        metrics::RESOLVE_METRIC_ID,
        vec![(
            metrics::ResolveDurationMetricDimensionResult::Success,
            metrics::ResolveDurationMetricDimensionResolverType::Regular,
        )],
    )
    .await;
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn resolve_failure_regular_unreachable() {
    let env = TestEnvBuilder::new().build().await;
    assert_eq!(
        env.resolve_package("fuchsia-pkg://example.com/missing").await.map(|_| ()),
        Err(Status::ADDRESS_UNREACHABLE),
    );

    assert_count_events(
        &env,
        metrics::RESOLVE_METRIC_ID,
        vec![(
            metrics::ResolveMetricDimensionResult::ZxErrAddressUnreachable,
            metrics::ResolveDurationMetricDimensionResolverType::Regular,
        )],
    )
    .await;
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn resolve_duration_success() {
    let env = TestEnvBuilder::new().build().await;

    let pkg = PackageBuilder::new("just_meta_far").build().await.expect("created pkg");
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo.server();
    let served_repository = served_repository.start().unwrap();
    let repo_url = "fuchsia-pkg://example.com".parse().unwrap();
    let config = served_repository.make_repo_config(repo_url);
    let () = env.proxies.repo_manager.add(config.clone().into()).await.unwrap().unwrap();

    assert_eq!(
        env.resolve_package(&format!("fuchsia-pkg://example.com/{}", pkg.name())).await.map(|_| ()),
        Ok(())
    );

    assert_elapsed_duration_events(
        &env,
        metrics::RESOLVE_DURATION_METRIC_ID,
        vec![(
            metrics::ResolveDurationMetricDimensionResult::Success,
            metrics::ResolveDurationMetricDimensionResolverType::Regular,
        )],
    )
    .await;
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn resolve_duration_failure() {
    let env = TestEnvBuilder::new().build().await;
    assert_eq!(
        env.resolve_package("fuchsia-pkg://example.com/missing").await.map(|_| ()),
        Err(Status::ADDRESS_UNREACHABLE),
    );

    assert_elapsed_duration_events(
        &env,
        metrics::RESOLVE_DURATION_METRIC_ID,
        vec![(
            metrics::ResolveDurationMetricDimensionResult::Failure,
            metrics::ResolveDurationMetricDimensionResolverType::Regular,
        )],
    )
    .await;
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn resolve_duration_font_test_failure() {
    let env = TestEnvBuilder::new().build().await;
    let (_, server) = create_endpoints().unwrap();
    assert_eq!(
        env.proxies
            .font_resolver
            .resolve(
                "fuchsia-pkg://example.com/some-nonexistent-pkg",
                &mut UpdatePolicy { fetch_if_absent: true, allow_old_versions: false },
                server,
            )
            .await
            .unwrap()
            .unwrap_err(),
        Status::NOT_FOUND.into_raw()
    );
    assert_elapsed_duration_events(
        &env,
        metrics::RESOLVE_DURATION_METRIC_ID,
        vec![(
            metrics::ResolveDurationMetricDimensionResult::Failure,
            metrics::ResolveDurationMetricDimensionResolverType::Font,
        )],
    )
    .await;
    env.stop().await;
}

// Fetching one blob successfully should emit one success fetch blob event.
#[fasync::run_singlethreaded(test)]
async fn pkg_resolver_fetch_blob_success() {
    verify_resolve_emits_cobalt_events_with_metric_id(
        PackageBuilder::new("just_meta_far").build().await.expect("created pkg"),
        Option::<handler::StaticResponseCode>::None,
        Ok(()),
        metrics::FETCH_BLOB_METRIC_ID,
        vec![metrics::FetchBlobMetricDimensionResult::Success],
    )
    .await;
}

// Fetching one blob with an HTTP error should emit 2 failure blob events (b/c of retries).
#[fasync::run_singlethreaded(test)]
async fn pkg_resolver_fetch_blob_failure() {
    let pkg = PackageBuilder::new("just_meta_far").build().await.expect("created pkg");
    let handler = handler::ForPath::new(
        format!("/blobs/{}", pkg.meta_far_merkle_root()),
        handler::StaticResponseCode::not_found(),
    );

    verify_resolve_emits_cobalt_events_with_metric_id(
        pkg,
        Some(handler),
        Err(Status::UNAVAILABLE),
        metrics::FETCH_BLOB_METRIC_ID,
        vec![
            metrics::FetchBlobMetricDimensionResult::BadHttpStatus,
            metrics::FetchBlobMetricDimensionResult::BadHttpStatus,
        ],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn merkle_for_url_success() {
    verify_resolve_emits_cobalt_events_with_metric_id(
        PackageBuilder::new("just_meta_far").build().await.expect("created pkg"),
        Option::<handler::StaticResponseCode>::None,
        Ok(()),
        metrics::MERKLE_FOR_URL_METRIC_ID,
        vec![metrics::MerkleForUrlMetricDimensionResult::Success],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn merkle_for_url_failure() {
    // Deleting the `targets` stanza from the targets metadata without updating the signature will
    // cause validation during the metadata update to fail, but the pkg-resolver does not fail a
    // resolve when metadata updates fail, instead the pkg-resolver will attempt to fetch the target
    // description from the tuf-client which will fail with `MissingMetadata(Targets)`.
    fn delete_targets_stanza(mut v: serde_json::Value) -> serde_json::Value {
        v["signed"]["targets"].take();
        v
    }
    verify_resolve_emits_cobalt_events_with_metric_id(
        PackageBuilder::new("just_meta_far").build().await.expect("created pkg"),
        Some(handler::ForPath::new("/2.targets.json", delete_targets_stanza)),
        Err(Status::INTERNAL),
        metrics::MERKLE_FOR_URL_METRIC_ID,
        vec![metrics::MerkleForUrlMetricDimensionResult::TufError],
    )
    .await;
}

// Resolving a package should trigger the creation of a TUF client.
#[fasync::run_singlethreaded(test)]
async fn create_tuf_client_success() {
    verify_resolve_emits_cobalt_events_with_metric_id(
        PackageBuilder::new("just_meta_far").build().await.expect("created pkg"),
        Option::<handler::StaticResponseCode>::None,
        Ok(()),
        metrics::CREATE_TUF_CLIENT_METRIC_ID,
        vec![metrics::CreateTufClientMetricDimensionResult::Success],
    )
    .await;
}

// Resolving a package should trigger the creation of a TUF client.
#[fasync::run_singlethreaded(test)]
async fn create_tuf_client_error() {
    let handler = handler::ForPath::new("/1.root.json", handler::StaticResponseCode::not_found());
    verify_resolve_emits_cobalt_events_with_metric_id(
        PackageBuilder::new("just_meta_far").build().await.expect("created pkg"),
        Some(handler),
        Err(Status::INTERNAL),
        metrics::CREATE_TUF_CLIENT_METRIC_ID,
        vec![metrics::CreateTufClientMetricDimensionResult::NotFound],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn update_tuf_client_success() {
    verify_resolve_emits_cobalt_events_with_metric_id(
        PackageBuilder::new("just_meta_far").build().await.expect("created pkg"),
        Option::<handler::StaticResponseCode>::None,
        Ok(()),
        metrics::UPDATE_TUF_CLIENT_METRIC_ID,
        vec![metrics::UpdateTufClientMetricDimensionResult::Success],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn update_tuf_client_error() {
    verify_resolve_emits_cobalt_events_with_metric_id(
        PackageBuilder::new("just_meta_far").build().await.expect("created pkg"),
        Some(handler::ForPath::new("/2.targets.json", handler::StaticResponseCode::not_found())),
        Err(Status::NOT_FOUND),
        metrics::UPDATE_TUF_CLIENT_METRIC_ID,
        vec![metrics::UpdateTufClientMetricDimensionResult::NotFound],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn font_resolver_is_font_package_check_not_font() {
    let env = TestEnvBuilder::new().build().await;
    let repo =
        Arc::new(RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH).build().await.unwrap());
    let served_repository = repo.server().start().unwrap();
    let () = env
        .proxies
        .repo_manager
        .add(
            served_repository.make_repo_config("fuchsia-pkg://example.com".parse().unwrap()).into(),
        )
        .await
        .unwrap()
        .unwrap();

    // No font packages have been registered with the font resolver, so resolves of any packages
    // (existing or not) will fail with NOT_FOUND and emit an event with the NotFont dimension.
    let (_, server) = create_endpoints().unwrap();
    assert_eq!(
        env.proxies
            .font_resolver
            .resolve(
                "fuchsia-pkg://example.com/some-nonexistent-pkg",
                &mut UpdatePolicy { fetch_if_absent: true, allow_old_versions: false },
                server,
            )
            .await
            .unwrap()
            .unwrap_err(),
        Status::NOT_FOUND.into_raw()
    );

    assert_count_events(
        &env,
        metrics::IS_FONT_PACKAGE_CHECK_METRIC_ID,
        vec![metrics::IsFontPackageCheckMetricDimensionResult::NotFont],
    )
    .await;
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn font_manager_load_static_registry_success() {
    let json = serde_json::to_string(&json!(["fuchsia-pkg://fuchsia.com/font1"])).unwrap();
    let env = TestEnvBuilder::new()
        .mounts(MountsBuilder::new().custom_config_data("font_packages.json", json).build())
        .build()
        .await;

    assert_count_events(
        &env,
        metrics::FONT_MANAGER_LOAD_STATIC_REGISTRY_METRIC_ID,
        vec![metrics::FontManagerLoadStaticRegistryMetricDimensionResult::Success],
    )
    .await;

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn font_manager_load_static_registry_failure_io() {
    let env = TestEnvBuilder::new().build().await;

    assert_count_events(
        &env,
        metrics::FONT_MANAGER_LOAD_STATIC_REGISTRY_METRIC_ID,
        vec![metrics::FontManagerLoadStaticRegistryMetricDimensionResult::Io],
    )
    .await;

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn font_manager_load_static_registry_failure_parse() {
    let env = TestEnvBuilder::new()
        .mounts(
            MountsBuilder::new().custom_config_data("font_packages.json", "invalid-json").build(),
        )
        .build()
        .await;

    assert_count_events(
        &env,
        metrics::FONT_MANAGER_LOAD_STATIC_REGISTRY_METRIC_ID,
        vec![metrics::FontManagerLoadStaticRegistryMetricDimensionResult::Parse],
    )
    .await;

    env.stop().await;
}

// We should get a cobalt event for each pkg-url error.
#[fasync::run_singlethreaded(test)]
async fn font_manager_load_static_registry_failure_pkg_url() {
    let json = serde_json::to_string(&json!([
        "fuchsia-pkg://includes-resource.com/foo#meta/resource.cmx"
    ]))
    .unwrap();
    let env = TestEnvBuilder::new()
        .mounts(MountsBuilder::new().custom_config_data("font_packages.json", json).build())
        .build()
        .await;

    assert_count_events(
        &env,
        metrics::FONT_MANAGER_LOAD_STATIC_REGISTRY_METRIC_ID,
        vec![metrics::FontManagerLoadStaticRegistryMetricDimensionResult::PkgUrl],
    )
    .await;

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn load_repository_for_channel_success_no_rewrite_rule() {
    let env = TestEnvBuilder::new().build().await;

    assert_count_events(
        &env,
        metrics::REPOSITORY_MANAGER_LOAD_REPOSITORY_FOR_CHANNEL_METRIC_ID,
        vec![metrics::RepositoryManagerLoadRepositoryForChannelMetricDimensionResult::Success],
    )
    .await;

    env.stop().await;
}
