// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests the Cobalt metrics reporting.
use {
    cobalt_client::traits::AsEventCode as _,
    cobalt_sw_delivery_registry as metrics,
    fidl_fuchsia_cobalt::{CobaltEvent, CountEvent, EventPayload},
    fuchsia_async as fasync,
    fuchsia_pkg_testing::{
        serve::{handler, UriPathHandler},
        Package, PackageBuilder, RepositoryBuilder,
    },
    fuchsia_zircon::{self as zx, Status},
    lib::{TestEnvBuilder, EMPTY_REPO_PATH},
    matches::assert_matches,
    std::sync::Arc,
};

#[fasync::run_singlethreaded(test)]
async fn pkg_resolver_startup_duration() {
    let env = TestEnvBuilder::new().build();

    loop {
        let events = env.mocks.logger_factory.events();
        if !events.is_empty() {
            let CobaltEvent { metric_id: _, event_codes, component, payload } = events
                .iter()
                .find(|CobaltEvent { metric_id, .. }| {
                    *metric_id == metrics::PKG_RESOLVER_STARTUP_DURATION_METRIC_ID
                })
                .unwrap();

            assert_eq!(event_codes, &vec![0]);
            assert_eq!(component, &None);
            assert_matches!(payload, EventPayload::ElapsedMicros(_));

            break;
        }
        fasync::Timer::new(fasync::Time::after(zx::Duration::from_millis(10))).await;
    }

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
