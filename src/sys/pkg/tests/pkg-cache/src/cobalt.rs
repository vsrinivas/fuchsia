// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests the Cobalt metrics reporting.
use {
    crate::TestEnv,
    blobfs_ramdisk::BlobfsRamdisk,
    cobalt_client::traits::AsEventCodes,
    cobalt_sw_delivery_registry as metrics,
    fidl_fuchsia_cobalt::{CobaltEvent, CountEvent, EventPayload},
    fuchsia_async as fasync,
    fuchsia_pkg_testing::SystemImageBuilder,
    fuchsia_zircon as zx,
    matches::assert_matches,
    pkgfs_ramdisk::PkgfsRamdisk,
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

#[fasync::run_singlethreaded(test)]
async fn pkg_cache_open_failure() {
    let env = TestEnv::builder().build();

    assert_eq!(
        env.open_package("0000000000000000000000000000000000000000000000000000000000000000")
            .await
            .map(|_| ()),
        Err(zx::Status::NOT_FOUND)
    );
    assert_count_events(
        &env,
        metrics::PKG_CACHE_OPEN_METRIC_ID,
        vec![metrics::PkgCacheOpenMetricDimensionResult::NotFound],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn pkg_cache_open_success() {
    let blobfs = BlobfsRamdisk::start().unwrap();
    let system_image_package = SystemImageBuilder::new();
    let system_image_package = system_image_package.build().await;
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();

    let env = TestEnv::builder().pkgfs(pkgfs).build();
    assert_eq!(
        env.open_package(&system_image_package.meta_far_merkle_root().clone().to_string())
            .await
            .map(|_| ()),
        Ok(())
    );

    assert_count_events(
        &env,
        metrics::PKG_CACHE_OPEN_METRIC_ID,
        vec![metrics::PkgCacheOpenMetricDimensionResult::Success],
    )
    .await;
}
