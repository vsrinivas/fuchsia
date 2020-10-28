// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::*, pretty_assertions::assert_eq};

#[fasync::run_singlethreaded(test)]
async fn validates_board() {
    let env = TestEnv::builder().build();

    env.set_board_name("x64");

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("board", "x64")
        .add_file("zbi", "fake zbi")
        .add_file("bootloader", "new bootloader");

    env.run_update().await.expect("success");

    assert_eq!(resolved_urls(Arc::clone(&env.interactions)), vec![UPDATE_PKG_URL]);
}

#[fasync::run_singlethreaded(test)]
async fn rejects_mismatched_board() {
    let env = TestEnv::builder().build();

    env.set_board_name("x64");

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("board", "arm")
        .add_file("zbi", "fake zbi")
        .add_file("bootloader", "new bootloader")
        .add_file("version", "1.2.3.4");

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    // Expect to have failed prior to downloading images.
    assert_eq!(resolved_urls(Arc::clone(&env.interactions)), vec![UPDATE_PKG_URL]);

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator: metrics::OtaResultAttemptsMetricDimensionInitiator::UserInitiatedCheck
                as u32,
            phase: metrics::OtaResultAttemptsMetricDimensionPhase::Tufupdate as u32,
            status_code: metrics::OtaResultAttemptsMetricDimensionStatusCode::Error as u32,
            target: "1.2.3.4".into(),
        }
    );
}
