// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::*, pretty_assertions::assert_eq};

#[fasync::run_singlethreaded(test)]
async fn rejects_invalid_package_name() {
    let env = TestEnv::builder().oneshot(true).build();

    // Name the update package something other than "update" and assert that the process fails to
    // validate the update package.
    env.resolver
        .register_custom_package("not_update", "not_update", "upd4t3", "fuchsia.com")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("zbi", "fake zbi")
        .add_file("zedboot", "new recovery");

    let not_update_package_url = "fuchsia-pkg://fuchsia.com/not_update";

    let result = env
        .run_system_updater_oneshot(SystemUpdaterArgs {
            initiator: Some(Initiator::User),
            target: Some("m3rk13"),
            update: Some(not_update_package_url),
            ..Default::default()
        })
        .await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    // Expect to have failed prior to downloading images.
    // The overall result should be similar to an invalid board, and we should have used
    // the not_update package URL, not `fuchsia.com/update`.
    assert_eq!(
        env.take_interactions(),
        vec![
            Paver(PaverEvent::QueryActiveConfiguration),
            Paver(PaverEvent::ReadAsset {
                configuration: paver::Configuration::A,
                asset: paver::Asset::VerifiedBootMetadata
            }),
            Paver(PaverEvent::ReadAsset {
                configuration: paver::Configuration::A,
                asset: paver::Asset::Kernel
            }),
            Gc,
            PackageResolve(not_update_package_url.to_string())
        ]
    );

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

#[fasync::run_singlethreaded(test)]
async fn fails_if_package_unavailable() {
    let env = TestEnv::builder().oneshot(true).build();

    env.resolver.mock_resolve_failure(UPDATE_PKG_URL, Status::NOT_FOUND);

    let result = env
        .run_system_updater_oneshot(SystemUpdaterArgs {
            initiator: Some(Initiator::User),
            target: Some("m3rk13"),
            ..Default::default()
        })
        .await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.take_interactions(),
        vec![
            Paver(PaverEvent::QueryActiveConfiguration),
            Paver(PaverEvent::ReadAsset {
                configuration: paver::Configuration::A,
                asset: paver::Asset::VerifiedBootMetadata
            }),
            Paver(PaverEvent::ReadAsset {
                configuration: paver::Configuration::A,
                asset: paver::Asset::Kernel
            }),
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
        ]
    );
}
