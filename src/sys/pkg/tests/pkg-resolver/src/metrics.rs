// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests the Cobalt metrics reporting.
use {
    cobalt_client::traits::AsEventCode as _,
    cobalt_sw_delivery_registry as metrics,
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_cobalt::{CobaltEvent, CountEvent, Event, EventPayload},
    fidl_fuchsia_pkg::UpdatePolicy,
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

async fn assert_repository_manager_load_static_configs_result(
    env: &TestEnv,
    result: metrics::RepositoryManagerLoadStaticConfigsMetricDimensionResult,
) {
    assert_eq!(
        env.mocks
            .logger_factory
            .wait_for_at_least_n_events_with_metric_id(
                1,
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

async fn verify_resolve_emits_cobalt_events_with_metric_id(
    pkg: Package,
    handler: Option<impl UriPathHandler>,
    expected_resolve_result: Result<(), Status>,
    metric_id: u32,
    expected_events: Vec<CobaltEvent>,
) {
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

    let received_events = env
        .mocks
        .logger_factory
        .wait_for_at_least_n_events_with_metric_id(expected_events.len(), metric_id)
        .await;
    assert_eq!(received_events, expected_events);
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

    verify_resolve_emits_cobalt_events_with_metric_id(
        pkg,
        Some(handler),
        Err(Status::UNAVAILABLE),
        metrics::FETCH_BLOB_METRIC_ID,
        vec![expected_event.clone(), expected_event],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn font_resolver_is_font_package_check_not_font() {
    let env = TestEnvBuilder::new().build();
    let repo =
        Arc::new(RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH).build().await.unwrap());
    let served_repository = repo.server().start().unwrap();
    env.proxies
        .repo_manager
        .add(
            served_repository.make_repo_config("fuchsia-pkg://example.com".parse().unwrap()).into(),
        )
        .await
        .unwrap();

    // No font packages have been registered with the font resolver, so resolves of any packages
    // (existing or not) will fail with NOT_FOUND and emit an event with the NotFont dimension.
    let (_, server) = create_endpoints().unwrap();
    assert_eq!(
        Status::from_raw(
            env.proxies
                .font_resolver
                .resolve(
                    "fuchsia-pkg://example.com/some-nonexistent-pkg",
                    &mut UpdatePolicy { fetch_if_absent: true, allow_old_versions: false },
                    server,
                )
                .await
                .unwrap()
        ),
        Status::NOT_FOUND
    );

    let received_events = env
        .mocks
        .logger_factory
        .wait_for_at_least_n_events_with_metric_id(1, metrics::IS_FONT_PACKAGE_CHECK_METRIC_ID)
        .await;
    assert_eq!(
        received_events,
        vec![CobaltEvent {
            metric_id: metrics::IS_FONT_PACKAGE_CHECK_METRIC_ID,
            event_codes: vec![
                metrics::IsFontPackageCheckMetricDimensionResult::NotFont.as_event_code(),
            ],
            component: None,
            payload: EventPayload::EventCount(CountEvent { period_duration_micros: 0, count: 1 }),
        }]
    );
    env.stop().await;
}
