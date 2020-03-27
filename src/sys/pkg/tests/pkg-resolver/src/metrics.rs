// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests the Cobalt metrics reporting.
use {
    fidl_fuchsia_cobalt::{CobaltEvent, EventPayload},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    lib::TestEnvBuilder,
    matches::assert_matches,
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
                    *metric_id
                        == cobalt_sw_delivery_registry::PKG_RESOLVER_STARTUP_DURATION_METRIC_ID
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
