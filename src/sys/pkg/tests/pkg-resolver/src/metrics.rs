// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests the Cobalt metrics reporting.
use {
    cobalt_client::traits::AsEventCodes,
    cobalt_sw_delivery_registry as metrics,
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_cobalt::{CobaltEvent, EventPayload},
    fuchsia_async as fasync,
    fuchsia_pkg_testing::{
        serve::{responder, HttpResponder},
        Package, PackageBuilder, RepositoryBuilder,
    },
    fuchsia_zircon::Status,
    lib::{make_repo, make_repo_config, MountsBuilder, TestEnv, TestEnvBuilder, EMPTY_REPO_PATH},
    matches::assert_matches,
    serde_json::json,
    std::{net::Ipv4Addr, sync::Arc},
};

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
    responder: Option<impl HttpResponder>,
    expected_resolve_result: Result<(), fidl_fuchsia_pkg::ResolveError>,
    metric_id: u32,
    expected_events: Vec<impl AsEventCodes>,
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
    if let Some(responder) = responder {
        served_repository = served_repository.response_overrider(responder);
    }
    let served_repository = served_repository.start().unwrap();
    let repo_url = "fuchsia-pkg://example.com".parse().unwrap();
    let config = served_repository.make_repo_config(repo_url);
    let () = env.proxies.repo_manager.add(config.clone().into()).await.unwrap().unwrap();

    assert_eq!(
        env.resolve_package(&format!("fuchsia-pkg://example.com/{}", pkg.name())).await.map(|_| ()),
        expected_resolve_result
    );
    env.assert_count_events(metric_id, expected_events).await;
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn repository_manager_load_static_configs_success() {
    let env = TestEnvBuilder::new()
        .mounts(lib::MountsBuilder::new().static_repository(make_repo()).build())
        .build()
        .await;

    env.assert_count_events(
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

    env.assert_count_events(
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

    env.assert_count_events(
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

    env.assert_count_events(
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

    env.assert_count_events(
        metrics::RESOLVE_STATUS_METRIC_ID,
        vec![(metrics::ResolveStatusMetricDimensionResult::Success,)],
    )
    .await;
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn resolve_failure_regular_unreachable() {
    let env = TestEnvBuilder::new().build().await;
    assert_eq!(
        env.resolve_package("fuchsia-pkg://example.com/missing").await.map(|_| ()),
        Err(fidl_fuchsia_pkg::ResolveError::RepoNotFound),
    );

    env.assert_count_events(
        metrics::RESOLVE_STATUS_METRIC_ID,
        vec![(metrics::ResolveStatusMetricDimensionResult::RepoNotFound,)],
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
        Err(fidl_fuchsia_pkg::ResolveError::RepoNotFound),
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
            .resolve("fuchsia-pkg://example.com/some-nonexistent-pkg", server,)
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
        Option::<responder::StaticResponseCode>::None,
        Ok(()),
        metrics::FETCH_BLOB_METRIC_ID,
        vec![(
            metrics::FetchBlobMetricDimensionResult::Success,
            metrics::FetchBlobMetricDimensionResumed::False,
        )],
    )
    .await;
}

// Fetching one blob with an HTTP error should emit 2 failure blob events (b/c of retries).
#[fasync::run_singlethreaded(test)]
async fn pkg_resolver_fetch_blob_failure() {
    let pkg = PackageBuilder::new("just_meta_far").build().await.expect("created pkg");
    let responder = responder::ForPath::new(
        format!("/blobs/{}", pkg.meta_far_merkle_root()),
        responder::StaticResponseCode::not_found(),
    );

    verify_resolve_emits_cobalt_events_with_metric_id(
        pkg,
        Some(responder),
        Err(fidl_fuchsia_pkg::ResolveError::UnavailableBlob),
        metrics::FETCH_BLOB_METRIC_ID,
        vec![
            (
                metrics::FetchBlobMetricDimensionResult::HttpNotFound,
                metrics::FetchBlobMetricDimensionResumed::False
            );
            2
        ],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn merkle_for_url_success() {
    verify_resolve_emits_cobalt_events_with_metric_id(
        PackageBuilder::new("just_meta_far").build().await.expect("created pkg"),
        Option::<responder::StaticResponseCode>::None,
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
        Some(responder::ForPath::new("/2.targets.json", delete_targets_stanza)),
        Err(fidl_fuchsia_pkg::ResolveError::Internal),
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
        Option::<responder::StaticResponseCode>::None,
        Ok(()),
        metrics::CREATE_TUF_CLIENT_METRIC_ID,
        vec![metrics::CreateTufClientMetricDimensionResult::Success],
    )
    .await;
}

// Resolving a package should trigger the creation of a TUF client.
#[fasync::run_singlethreaded(test)]
async fn create_tuf_client_error() {
    let responder =
        responder::ForPath::new("/1.root.json", responder::StaticResponseCode::not_found());
    verify_resolve_emits_cobalt_events_with_metric_id(
        PackageBuilder::new("just_meta_far").build().await.expect("created pkg"),
        Some(responder),
        Err(fidl_fuchsia_pkg::ResolveError::Internal),
        metrics::CREATE_TUF_CLIENT_METRIC_ID,
        vec![metrics::CreateTufClientMetricDimensionResult::NotFound],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn update_tuf_client_success() {
    verify_resolve_emits_cobalt_events_with_metric_id(
        PackageBuilder::new("just_meta_far").build().await.expect("created pkg"),
        Option::<responder::StaticResponseCode>::None,
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
        Some(responder::ForPath::new(
            "/2.targets.json",
            responder::StaticResponseCode::not_found(),
        )),
        Err(fidl_fuchsia_pkg::ResolveError::PackageNotFound),
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
            .resolve("fuchsia-pkg://example.com/some-nonexistent-pkg", server,)
            .await
            .unwrap()
            .unwrap_err(),
        Status::NOT_FOUND.into_raw()
    );

    env.assert_count_events(
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

    env.assert_count_events(
        metrics::FONT_MANAGER_LOAD_STATIC_REGISTRY_METRIC_ID,
        vec![metrics::FontManagerLoadStaticRegistryMetricDimensionResult::Success],
    )
    .await;

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn font_manager_load_static_registry_failure_io() {
    let env = TestEnvBuilder::new().build().await;

    env.assert_count_events(
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

    env.assert_count_events(
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

    env.assert_count_events(
        metrics::FONT_MANAGER_LOAD_STATIC_REGISTRY_METRIC_ID,
        vec![metrics::FontManagerLoadStaticRegistryMetricDimensionResult::PkgUrl],
    )
    .await;

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn load_repository_for_channel_success_no_rewrite_rule() {
    let env = TestEnvBuilder::new().build().await;

    env.assert_count_events(
        metrics::REPOSITORY_MANAGER_LOAD_REPOSITORY_FOR_CHANNEL_METRIC_ID,
        vec![metrics::RepositoryManagerLoadRepositoryForChannelMetricDimensionResult::Success],
    )
    .await;

    env.stop().await;
}

// Test the HTTP status range space for metrics handling for a blob fetch.
mod pkg_resolver_blob_fetch {
    use super::*;

    struct StatusTest {
        min_code: u16,
        max_code: u16,
        count: usize,
        status: metrics::FetchBlobMetricDimensionResult,
    }

    // Macro to construct a test table table.
    macro_rules! test_cases {
        ( $( $name:ident => [ $( ( $min:expr, $max:expr, $count:expr, $status:ident ), )+ ], )+ ) => {
            $(
                #[fasync::run_singlethreaded(test)]
                async fn $name() {
                    let test_table: Vec<StatusTest> = vec![
                        $(
                            StatusTest {
                                min_code: $min,
                                max_code: $max,
                                count: $count,
                                status: metrics::FetchBlobMetricDimensionResult::$status,
                            },
                        )+
                    ];

                    verify_status_ranges(&test_table).await
                }
            )+
        }
    }

    // Test cases are:
    // (start code, end code, number of expected metrics, metric name)
    test_cases! {
        status_ranges_101_2xx => [
            // Hyper doesn't support 100-level responses other than protocol upgrade, and silently
            // turns them into a 500 and closes the connection. That results in flakiness when we can
            // request faster than the connection is removed from the pool.
            (101, 101, 2, Http1xx),
            // We're not sending a body, so we expect a C-L issue rather than Success.
            (200, 200, 1, ContentLengthMismatch),
            (201, 299, 2, Http2xx),
        ],
        status_ranges_3xx => [
            (300, 399, 2, Http3xx),
        ],
        status_ranges_4xx => [
            (400, 400, 2, HttpBadRequest),
            (401, 401, 2, HttpUnauthorized),
            (402, 402, 2, Http4xx),
            (403, 403, 2, HttpForbidden),
            (404, 404, 2, HttpNotFound),
            (405, 405, 2, HttpMethodNotAllowed),
            (406, 407, 2, Http4xx),
            (408, 408, 2, HttpRequestTimeout),
            (409, 411, 2, Http4xx),
            (412, 412, 2, HttpPreconditionFailed),
            (413, 415, 2, Http4xx),
            (416, 416, 2, HttpRangeNotSatisfiable),
            (417, 428, 2, Http4xx),
            // We expect 4 metrics here because this response triggers a different retry policy.
            (429, 429, 4, HttpTooManyRequests),
            (430, 499, 2, Http4xx),
        ],
        status_ranges_5xx => [
            (500, 500, 2, HttpInternalServerError),
            (501, 501, 2, Http5xx),
            (502, 502, 2, HttpBadGateway),
            (503, 503, 2, HttpServiceUnavailable),
            (504, 504, 2, HttpGatewayTimeout),
            (505, 599, 2, Http5xx),
        ],
        // 600-999 aren't real, but are sometimes used in e.g. CDN configurations to track state
        // machine transitions, and occasionally leak on bugs. Unfortunately, we don't get to test
        // these because StatusCode won't let us create new ones in this range.
    }

    async fn verify_status_ranges(test_table: &[StatusTest]) {
        let pkg = PackageBuilder::new("just_meta_far").build().await.expect("created pkg");
        let env = TestEnvBuilder::new().build().await;
        let repo = Arc::new(
            RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
                .add_package(&pkg)
                .build()
                .await
                .unwrap(),
        );
        let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());

        let (responder, response_code) = responder::DynamicResponseCode::new(100);
        let served_repository = repo
            .server()
            .bind_to_addr(Ipv4Addr::LOCALHOST)
            .response_overrider(responder::ForPath::new(
                format!("/blobs/{}", pkg.meta_far_merkle_root()),
                responder,
            ))
            .start()
            .unwrap();
        env.register_repo(&served_repository).await;

        let mut statuses = Vec::new();

        for ent in test_table.iter() {
            for code in ent.min_code..=ent.max_code {
                response_code.set(code);
                let _ = env.resolve_package(&pkg_url).await;
                statuses.append(&mut vec![
                    (
                        ent.status,
                        metrics::FetchBlobMetricDimensionResumed::False
                    );
                    ent.count
                ]);
            }
        }

        env.assert_count_events(metrics::FETCH_BLOB_METRIC_ID, statuses).await;

        env.stop().await;
    }
}

// Test the HTTP status range space for metrics related to TUF client construction.
mod pkg_resolver_create_tuf_client {
    use super::*;

    struct StatusTest {
        min_code: u16,
        max_code: u16,
        status: metrics::CreateTufClientMetricDimensionResult,
    }

    // Macro to construct a test table table.
    macro_rules! test_cases {
        ( $( $name:ident => [ $( ( $min:expr, $max:expr, $status:ident ), )+ ], )+ ) => {
            $(
                #[fasync::run_singlethreaded(test)]
                async fn $name() {
                    let test_table: Vec<StatusTest> = vec![
                        $(
                            StatusTest {
                                min_code: $min,
                                max_code: $max,
                                status: metrics::CreateTufClientMetricDimensionResult::$status,
                            },
                        )+
                    ];

                    verify_status_ranges(&test_table).await
                }
            )+
        }
    }

    // Test cases are:
    // (start code, end code, metric name)
    test_cases! {
        status_ranges_101_2xx => [
            // Hyper doesn't support 100-level responses other than protocol upgrade, and silently
            // turns them into a 500 and closes the connection. That results in flakiness when we can
            // request faster than the connection is removed from the pool.
            (101, 101, Http1xx),
            // We're not sending a body, so our empty response will report as an encoding issue.
            (200, 200, Encoding),
            (201, 299, Http2xx),
        ],
        status_ranges_3xx => [
            (300, 399, Http3xx),
        ],
        status_ranges_4xx => [
            (400, 400, HttpBadRequest),
            (401, 401, HttpUnauthorized),
            (402, 402, Http4xx),
            (403, 403, HttpForbidden),
            // rust-tuf returns its own NotFound for this, so we never use HttpNotFound.
            (404, 404, NotFound),
            (405, 405, HttpMethodNotAllowed),
            (406, 407, Http4xx),
            (408, 408, HttpRequestTimeout),
            (409, 411, Http4xx),
            (412, 412, HttpPreconditionFailed),
            (413, 415, Http4xx),
            (416, 416, HttpRangeNotSatisfiable),
            (417, 428, Http4xx),
            (429, 429, HttpTooManyRequests),
            (430, 499, Http4xx),
        ],
        status_ranges_5xx => [
            (500, 500, HttpInternalServerError),
            (501, 501, Http5xx),
            (502, 502, HttpBadGateway),
            (503, 503, HttpServiceUnavailable),
            (504, 504, HttpGatewayTimeout),
            (505, 599, Http5xx),
        ],
        // 600-999 aren't real, but are sometimes used in e.g. CDN configurations to track state
        // machine transitions, and occasionally leak on bugs. Unfortunately, we don't get to test
        // these because StatusCode won't let us create new ones in this range.
    }

    async fn verify_status_ranges(test_table: &[StatusTest]) {
        let pkg = PackageBuilder::new("just_meta_far").build().await.expect("created pkg");
        let env = TestEnvBuilder::new().build().await;
        let repo = Arc::new(
            RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
                .add_package(&pkg)
                .build()
                .await
                .unwrap(),
        );
        let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());

        let (responder, response_code) = responder::DynamicResponseCode::new(100);
        let served_repository = repo
            .server()
            .bind_to_addr(Ipv4Addr::LOCALHOST)
            .response_overrider(responder::ForPath::new("/1.root.json", responder))
            .start()
            .unwrap();
        env.register_repo(&served_repository).await;

        let mut statuses: Vec<metrics::CreateTufClientMetricDimensionResult> = Vec::new();

        for ent in test_table.iter() {
            for code in ent.min_code..=ent.max_code {
                response_code.set(code);
                let _ = env.resolve_package(&pkg_url).await;
                statuses.push(ent.status);
            }
        }

        env.assert_count_events(metrics::CREATE_TUF_CLIENT_METRIC_ID, statuses).await;

        env.stop().await;
    }
}

// Test the HTTP status range space for metrics related to TUF update clients.
mod pkg_resolver_update_tuf_client {
    use super::*;

    struct StatusTest {
        min_code: u16,
        max_code: u16,
        status: metrics::UpdateTufClientMetricDimensionResult,
    }

    // Macro to construct a test table table.
    macro_rules! test_cases {
        ( $( $name:ident => [ $( ( $min:expr, $max:expr, $status:ident ), )+ ], )+ ) => {
            $(
                #[fasync::run_singlethreaded(test)]
                async fn $name() {
                    let test_table: Vec<StatusTest> = vec![
                        $(
                            StatusTest {
                                min_code: $min,
                                max_code: $max,
                                status: metrics::UpdateTufClientMetricDimensionResult::$status,
                            },
                        )+
                    ];

                    verify_status_ranges(&test_table).await
                }
            )+
        }
    }

    // Test cases are:
    // (start code, end code, metric name)
    test_cases! {
        status_ranges_101_2xx => [
            // Hyper doesn't support 100-level responses other than protocol upgrade, and silently
            // turns them into a 500 and closes the connection. That results in flakiness when we can
            // request faster than the connection is removed from the pool.
            (101, 101, Http1xx),
            // We're not sending a body, so our empty response will report as an encoding issue.
            (200, 200, Encoding),
            (201, 299, Http2xx),
        ],
        status_ranges_3xx => [
            (300, 399, Http3xx),
        ],
        status_ranges_4xx => [
            (400, 400, HttpBadRequest),
            (401, 401, HttpUnauthorized),
            (402, 402, Http4xx),
            (403, 403, HttpForbidden),
            // It's fine if this file doesn't exist.
            (404, 404, Success),
            (405, 405, HttpMethodNotAllowed),
            (406, 407, Http4xx),
            (408, 408, HttpRequestTimeout),
            (409, 411, Http4xx),
            (412, 412, HttpPreconditionFailed),
            (413, 415, Http4xx),
            (416, 416, HttpRangeNotSatisfiable),
            (417, 428, Http4xx),
            (429, 429, HttpTooManyRequests),
            (430, 499, Http4xx),
        ],
        status_ranges_5xx => [
            (500, 500, HttpInternalServerError),
            (501, 501, Http5xx),
            (502, 502, HttpBadGateway),
            (503, 503, HttpServiceUnavailable),
            (504, 504, HttpGatewayTimeout),
            (505, 599, Http5xx),
        ],
        // 600-999 aren't real, but are sometimes used in e.g. CDN configurations to track state
        // machine transitions, and occasionally leak on bugs. Unfortunately, we don't get to test
        // these because StatusCode won't let us create new ones in this range.
    }

    async fn verify_status_ranges(test_table: &[StatusTest]) {
        let pkg = PackageBuilder::new("just_meta_far").build().await.expect("created pkg");
        let env = TestEnvBuilder::new().build().await;
        let repo = Arc::new(
            RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
                .add_package(&pkg)
                .build()
                .await
                .unwrap(),
        );
        let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());

        let (responder, response_code) = responder::DynamicResponseCode::new(100);
        let served_repository = repo
            .server()
            .bind_to_addr(Ipv4Addr::LOCALHOST)
            .response_overrider(responder::ForPath::new("/2.root.json", responder))
            .start()
            .unwrap();

        let mut statuses: Vec<metrics::UpdateTufClientMetricDimensionResult> = Vec::new();

        for ent in test_table.iter() {
            for code in ent.min_code..=ent.max_code {
                // After the package resolver successfully updates TUF, it will automatically cache the
                // fetched metadata for a period of time in order to reduce load on the repository
                // server. Since we want each of these resolves to talk to our test server, we'll
                // re-register the repository before each request in order to reset the timer.
                env.register_repo(&served_repository).await;

                response_code.set(code);
                let _ = env.resolve_package(&pkg_url).await;
                statuses.push(ent.status);
            }
        }

        env.assert_count_events(metrics::UPDATE_TUF_CLIENT_METRIC_ID, statuses).await;

        env.stop().await;
    }
}
