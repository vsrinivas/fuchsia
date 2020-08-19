// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    fidl_fuchsia_update_installer_ext::{start_update, StateId},
    pretty_assertions::assert_eq,
};

fn force_recovery_json() -> String {
    json!({
      "version": "1",
      "content": {
        "mode": "force-recovery",
      }
    })
    .to_string()
}

#[fasync::run_singlethreaded(test)]
async fn writes_recovery_and_force_reboots_into_it() {
    let env = TestEnv::builder().oneshot(true).build();

    let package_url = SYSTEM_IMAGE_URL;
    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages", package_url)
        .add_file("update-mode", &force_recovery_json())
        .add_file("recovery", "the recovery image")
        .add_file("recovery.vbmeta", "the recovery vbmeta");

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
            Gc,
            Paver(PaverEvent::QueryActiveConfiguration),
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::Recovery,
                asset: paver::Asset::Kernel,
                payload: b"the recovery image".to_vec(),
            }),
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::Recovery,
                asset: paver::Asset::VerifiedBootMetadata,
                payload: b"the recovery vbmeta".to_vec(),
            }),
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::A
            }),
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::B
            }),
            Paver(PaverEvent::DataSinkFlush),
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn reboots_regardless_of_reboot_arg() {
    let env = TestEnv::builder().oneshot(true).build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages", "")
        .add_file("update-mode", &force_recovery_json());

    env.run_system_updater_oneshot(SystemUpdaterArgs {
        initiator: Some(Initiator::User),
        target: Some("m3rk13"),
        reboot: Some(false),
        ..Default::default()
    })
    .await
    .expect("run system updater");

    // Verify we made a reboot call.
    assert_eq!(env.take_interactions().last().unwrap(), &Reboot);
}

#[fasync::run_singlethreaded(test)]
async fn reboots_regardless_of_reboot_controller() {
    let env = TestEnv::builder().build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages", "")
        .add_file("update-mode", &force_recovery_json());

    // Start the system update.
    let (reboot_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    let attempt = start_update(
        &UPDATE_PKG_URL.parse().unwrap(),
        default_options(),
        env.installer_proxy(),
        Some(server_end),
    )
    .await
    .unwrap();
    let () = reboot_proxy.detach().unwrap();

    // Ensure the update attempt has completed.
    assert_eq!(
        attempt.map(|res| res.unwrap()).collect::<Vec<_>>().await.last().unwrap().id(),
        StateId::Reboot
    );
    assert_eq!(env.take_interactions().last().unwrap(), &Reboot);
}

#[fasync::run_singlethreaded(test)]
async fn rejects_zbi() {
    let env = TestEnv::builder().oneshot(true).build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file(
            "packages",
            "system_image/0=42ade6f4fd51636f70c68811228b4271ed52c4eb9a647305123b4f4d0741f296\n",
        )
        .add_file("update-mode", &force_recovery_json())
        .add_file("bootloader", "new bootloader")
        .add_file("zbi", "fake zbi");

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
            Gc
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn rejects_skip_recovery_flag() {
    let env = TestEnv::builder().oneshot(true).build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages", "")
        .add_file("update-mode", &force_recovery_json());

    let result = env
        .run_system_updater_oneshot(SystemUpdaterArgs {
            initiator: Some(Initiator::User),
            target: Some("m3rk13"),
            skip_recovery: Some(true),
            ..Default::default()
        })
        .await;
    assert!(result.is_err(), "system updater succeeded when it should fail");
}
