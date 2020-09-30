// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::*, pretty_assertions::assert_eq};

#[fasync::run_singlethreaded(test)]
async fn validates_board() {
    let env = TestEnv::builder().oneshot(true).build();

    env.set_board_name("x64");

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("board", "x64")
        .add_file("zbi", "fake zbi")
        .add_file("bootloader", "new bootloader");

    env.run_system_updater_oneshot(SystemUpdaterArgs {
        initiator: Some(Initiator::User),
        target: Some("m3rk13"),
        ..Default::default()
    })
    .await
    .expect("success");

    assert_eq!(resolved_urls(Arc::clone(&env.interactions)), vec![UPDATE_PKG_URL]);
}

#[fasync::run_singlethreaded(test)]
async fn rejects_mismatched_board() {
    let env = TestEnv::builder().oneshot(true).build();

    env.set_board_name("x64");

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("board", "arm")
        .add_file("zbi", "fake zbi")
        .add_file("bootloader", "new bootloader");

    let result = env
        .run_system_updater_oneshot(SystemUpdaterArgs {
            initiator: Some(Initiator::User),
            target: Some("m3rk13"),
            ..Default::default()
        })
        .await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    // Expect to have failed prior to downloading images.
    assert_eq!(resolved_urls(Arc::clone(&env.interactions)), vec![UPDATE_PKG_URL]);

    let loggers = env.logger_factory.loggers.lock().clone();
    assert_eq!(loggers.len(), 1);
    let logger = loggers.into_iter().next().unwrap();
    let events = OtaMetrics::from_events(logger.cobalt_events.lock().clone());
    assert_eq!(
        events,
        OtaMetrics {
            initiator: metrics::OtaResultAttemptsMetricDimensionInitiator::UserInitiatedCheck
                as u32,
            phase: metrics::OtaResultAttemptsMetricDimensionPhase::Tufupdate as u32,
            status_code: metrics::OtaResultAttemptsMetricDimensionStatusCode::Error as u32,
            target: "m3rk13".into(),
        }
    );
}
