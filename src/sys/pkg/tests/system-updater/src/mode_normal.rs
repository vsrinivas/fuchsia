// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::*, pretty_assertions::assert_eq};

#[fasync::run_singlethreaded(test)]
async fn updates_the_system() {
    let env = TestEnv::new();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file(
            "packages",
            "system_image/0=42ade6f4fd51636f70c68811228b4271ed52c4eb9a647305123b4f4d0741f296\n",
        )
        .add_file("zbi", "fake zbi");

    env.run_system_updater(SystemUpdaterArgs {
        initiator: "manual",
        target: "m3rk13",
        update: None,
        reboot: None,
        skip_recovery: None,
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
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            PackageResolve(SYSTEM_IMAGE_URL.to_string()),
            BlobfsSync,
            Paver(PaverEvent::QueryActiveConfiguration),
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
    let env = TestEnv::new();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file(
            "packages",
            "system_image/0=42ade6f4fd51636f70c68811228b4271ed52c4eb9a647305123b4f4d0741f296\n",
        )
        .add_file("bootloader", "new bootloader");

    let result = env
        .run_system_updater(SystemUpdaterArgs {
            initiator: "manual",
            target: "m3rk13",
            update: None,
            reboot: None,
            skip_recovery: None,
        })
        .await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.take_interactions(),
        vec![
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            PackageResolve(SYSTEM_IMAGE_URL.to_string()),
            BlobfsSync,
        ]
    );
}
