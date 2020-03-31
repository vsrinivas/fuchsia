// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests the Cobalt metrics reporting.
use {
    cobalt_client::traits::AsEventCode as _,
    cobalt_sw_delivery_registry as metrics,
    fidl_fuchsia_cobalt::{CobaltEvent, CountEvent, Event, EventPayload},
    fuchsia_async as fasync,
    fuchsia_pkg_testing::{
        serve::{handler, UriPathHandler},
        Package, PackageBuilder, RepositoryBuilder,
    },
    fuchsia_zircon::Status,
    lib::{make_repo, make_repo_config, MountsBuilder, TestEnv, TestEnvBuilder, EMPTY_REPO_PATH},
    matches::assert_matches,
    std::sync::Arc,
};

#[fasync::run_singlethreaded(test)]
async fn pkg_resolver_startup_duration() {
    let env = TestEnvBuilder::new().build();

    let events = env
        .mocks
        .logger_factory
        .wait_for_at_least_one_event_with_metric_id(
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

async fn assert_repository_manager_load_static_configs_result(
    env: &TestEnv,
    result: metrics::RepositoryManagerLoadStaticConfigsMetricDimensionResult,
) {
    assert_eq!(
        env.mocks
            .logger_factory
            .wait_for_at_least_one_event_with_metric_id(
                metrics::REPOSITORY_MANAGER_LOAD_STATIC_CONFIGS_METRIC_ID,
            )
            .await,
        vec![CobaltEvent {
            metric_id: metrics::REPOSITORY_MANAGER_LOAD_STATIC_CONFIGS_METRIC_ID,
            event_codes: vec![result.as_event_code()],
            component: None,
            payload: EventPayload::Event(Event {})
        }]
    );
}

#[fasync::run_singlethreaded(test)]
async fn repository_manager_load_static_configs_success() {
    let env = TestEnvBuilder::new()
        .mounts(lib::MountsBuilder::new().static_repository(make_repo()).build())
        .build();

    assert_repository_manager_load_static_configs_result(
        &env,
        metrics::RepositoryManagerLoadStaticConfigsMetricDimensionResult::Success,
    )
    .await;

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn repository_manager_load_static_configs_io() {
    let env = TestEnvBuilder::new().build();

    assert_repository_manager_load_static_configs_result(
        &env,
        metrics::RepositoryManagerLoadStaticConfigsMetricDimensionResult::Io,
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
        .build();

    assert_repository_manager_load_static_configs_result(
        &env,
        metrics::RepositoryManagerLoadStaticConfigsMetricDimensionResult::Parse,
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
        .build();

    assert_repository_manager_load_static_configs_result(
        &env,
        metrics::RepositoryManagerLoadStaticConfigsMetricDimensionResult::Overridden,
    )
    .await;

    env.stop().await;
}

async fn verify_resolve_emits_cobalt_events<H, F>(
    pkg: Package,
    handler: Option<H>,
    expected_resolve_result: Result<(), Status>,
    filter_fn: F,
    expected_events: Vec<CobaltEvent>,
) where
    H: UriPathHandler,
    F: FnMut(&CobaltEvent) -> bool,
{
    let env = TestEnvBuilder::new().build();
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
    env.proxies.repo_manager.add(config.clone().into()).await.unwrap();

    assert_eq!(
        env.resolve_package(&format!("fuchsia-pkg://example.com/{}", pkg.name())).await.map(|_| ()),
        expected_resolve_result
    );

    let received_events: Vec<CobaltEvent> =
        env.mocks.logger_factory.events().into_iter().filter(filter_fn).collect();
    assert_eq!(received_events, expected_events);
    env.stop().await;
}

// Fetching one blob successfully should emit one success fetch blob event.
#[fasync::run_singlethreaded(test)]
async fn pkg_resolver_fetch_blob_success() {
    verify_resolve_emits_cobalt_events(
        PackageBuilder::new("just_meta_far").build().await.expect("created pkg"),
        Option::<handler::StaticResponseCode>::None,
        Ok(()),
        |CobaltEvent { metric_id, .. }| *metric_id == metrics::FETCH_BLOB_METRIC_ID,
        vec![CobaltEvent {
            metric_id: metrics::FETCH_BLOB_METRIC_ID,
            event_codes: vec![metrics::FetchBlobMetricDimensionResult::Success.as_event_code()],
            component: None,
            payload: EventPayload::EventCount(CountEvent { period_duration_micros: 0, count: 1 }),
        }],
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
    let expected_event = CobaltEvent {
        metric_id: metrics::FETCH_BLOB_METRIC_ID,
        event_codes: vec![metrics::FetchBlobMetricDimensionResult::BadHttpStatus.as_event_code()],
        component: None,
        payload: EventPayload::EventCount(CountEvent { period_duration_micros: 0, count: 1 }),
    };

    verify_resolve_emits_cobalt_events(
        pkg,
        Some(handler),
        Err(Status::UNAVAILABLE),
        |CobaltEvent { metric_id, .. }| *metric_id == metrics::FETCH_BLOB_METRIC_ID,
        vec![expected_event.clone(), expected_event],
    )
    .await;
}
