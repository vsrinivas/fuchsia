// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    crate::progress_reporting::assert_success_monitor_states,
    fidl_fuchsia_update_installer_ext::{start_update, StateId},
    pretty_assertions::assert_eq,
};

#[fasync::run_singlethreaded(test)]
async fn updates_the_system() {
    let env = TestEnv::builder().oneshot(true).build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("zbi", "fake zbi");
    env.resolver
        .url(SYSTEM_IMAGE_URL)
        .resolve(&env.resolver.package("system_image/0", SYSTEM_IMAGE_HASH));

    env.run_system_updater_oneshot(SystemUpdaterArgs {
        initiator: Some(Initiator::User),
        target: Some("m3rk13"),
        ..Default::default()
    })
    .await
    .expect("run system updater");

    let loggers = env.logger_factory.loggers.lock().clone();
    assert_eq!(loggers.len(), 1);
    let logger = loggers.into_iter().next().unwrap();
    assert_eq!(
        OtaMetrics::from_events(logger.cobalt_events.lock().clone()),
        OtaMetrics {
            initiator: metrics::OtaResultAttemptsMetricDimensionInitiator::UserInitiatedCheck
                as u32,
            phase: metrics::OtaResultAttemptsMetricDimensionPhase::SuccessPendingReboot as u32,
            status_code: metrics::OtaResultAttemptsMetricDimensionStatusCode::Success as u32,
            target: "m3rk13".into(),
        }
    );

    assert_eq!(
        env.take_interactions(),
        vec![
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::ReadAsset {
                configuration: paver::Configuration::A,
                asset: paver::Asset::VerifiedBootMetadata
            }),
            Paver(PaverEvent::ReadAsset {
                configuration: paver::Configuration::A,
                asset: paver::Asset::Kernel
            }),
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::QueryActiveConfiguration),
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            PackageResolve(SYSTEM_IMAGE_URL.to_string()),
            BlobfsSync,
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::B,
                asset: paver::Asset::Kernel,
                payload: b"fake zbi".to_vec(),
            }),
            Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
            Paver(PaverEvent::DataSinkFlush),
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn requires_zbi() {
    let env = TestEnv::builder().oneshot(true).build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("bootloader", "new bootloader");
    env.resolver
        .url(SYSTEM_IMAGE_URL)
        .resolve(&env.resolver.package("system_image/0", SYSTEM_IMAGE_HASH));

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
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::ReadAsset {
                configuration: paver::Configuration::A,
                asset: paver::Asset::VerifiedBootMetadata
            }),
            Paver(PaverEvent::ReadAsset {
                configuration: paver::Configuration::A,
                asset: paver::Asset::Kernel
            }),
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::QueryActiveConfiguration),
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            PackageResolve(SYSTEM_IMAGE_URL.to_string()),
            BlobfsSync,
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn updates_the_system_no_oneshot() {
    let env = TestEnv::builder().build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("zbi", "fake zbi");
    env.resolver
        .url(SYSTEM_IMAGE_URL)
        .resolve(&env.resolver.package("system_image/0", SYSTEM_IMAGE_HASH));

    // Start the system update.
    let attempt = start_update(
        &UPDATE_PKG_URL.parse().unwrap(),
        default_options(),
        &env.installer_proxy(),
        None,
    )
    .await
    .unwrap();

    // Verify progress reporting events.
    assert_success_monitor_states(
        attempt.map(|res| res.unwrap()).collect().await,
        &[StateId::Prepare, StateId::Fetch, StateId::Stage, StateId::WaitToReboot, StateId::Reboot],
    );

    // Verify metrics reported.
    let loggers = env.logger_factory.loggers.lock().clone();
    assert_eq!(loggers.len(), 1);
    let logger = loggers.into_iter().next().unwrap();
    assert_eq!(
        OtaMetrics::from_events(logger.cobalt_events.lock().clone()),
        OtaMetrics {
            initiator: metrics::OtaResultAttemptsMetricDimensionInitiator::UserInitiatedCheck
                as u32,
            phase: metrics::OtaResultAttemptsMetricDimensionPhase::SuccessPendingReboot as u32,
            status_code: metrics::OtaResultAttemptsMetricDimensionStatusCode::Success as u32,
            target: "".into(),
        }
    );

    // Verify FIDL calls made.
    assert_eq!(
        env.take_interactions(),
        vec![
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::ReadAsset {
                configuration: paver::Configuration::A,
                asset: paver::Asset::VerifiedBootMetadata
            }),
            Paver(PaverEvent::ReadAsset {
                configuration: paver::Configuration::A,
                asset: paver::Asset::Kernel
            }),
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::QueryActiveConfiguration),
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            PackageResolve(SYSTEM_IMAGE_URL.to_string()),
            BlobfsSync,
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::B,
                asset: paver::Asset::Kernel,
                payload: b"fake zbi".to_vec(),
            }),
            Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
            Paver(PaverEvent::DataSinkFlush),
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}
