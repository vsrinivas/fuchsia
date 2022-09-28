// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    fidl_fuchsia_update_installer_ext::{start_update, StateId},
    pretty_assertions::assert_eq,
};

#[fasync::run_singlethreaded(test)]
async fn writes_recovery_and_force_reboots_into_it_v1() {
    let env = TestEnv::builder().build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("update-mode", &force_recovery_json())
        .add_file("recovery", "the recovery image")
        .add_file("recovery.vbmeta", "the recovery vbmeta");

    env.run_update().await.expect("run system updater");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator:
                metrics::OtaResultAttemptsMigratedMetricDimensionInitiator::UserInitiatedCheck
                    as u32,
            phase: metrics::OtaResultAttemptsMigratedMetricDimensionPhase::SuccessPendingReboot
                as u32,
            status_code: metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode::Success
                as u32,
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
            Paver(PaverEvent::QueryConfigurationStatus { configuration: paver::Configuration::A }),
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::B
            }),
            Paver(PaverEvent::BootManagerFlush),
            PackageResolve(UPDATE_PKG_URL.to_string()),
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
            Paver(PaverEvent::DataSinkFlush),
            ReplaceRetainedPackages(vec![]),
            Gc,
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::A
            }),
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::B
            }),
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn writes_recovery_and_force_reboots_into_it() {
    let env = TestEnv::builder().build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("update-mode", &force_recovery_json())
        .add_file("images.json", make_images_json_recovery());

    env.run_update().await.expect("run system updater");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator:
                metrics::OtaResultAttemptsMigratedMetricDimensionInitiator::UserInitiatedCheck
                    as u32,
            phase: metrics::OtaResultAttemptsMigratedMetricDimensionPhase::SuccessPendingReboot
                as u32,
            status_code: metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode::Success
                as u32,
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
            Paver(PaverEvent::QueryConfigurationStatus { configuration: paver::Configuration::A }),
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::B
            }),
            Paver(PaverEvent::BootManagerFlush),
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Paver(PaverEvent::ReadAsset {
                configuration: paver::Configuration::Recovery,
                asset: paver::Asset::Kernel,
            }),
            Paver(PaverEvent::DataSinkFlush),
            ReplaceRetainedPackages(vec![]),
            Gc,
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::A
            }),
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::B
            }),
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn reboots_regardless_of_reboot_arg() {
    let env = TestEnv::builder().build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("update-mode", &force_recovery_json());

    env.run_update().await.expect("run system updater");

    // Verify we made a reboot call.
    assert_eq!(env.take_interactions().last().unwrap(), &Reboot);
}

#[fasync::run_singlethreaded(test)]
async fn reboots_regardless_of_reboot_controller() {
    let env = TestEnv::builder().build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("update-mode", &force_recovery_json());

    // Start the system update.
    let (reboot_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    let attempt = start_update(
        &UPDATE_PKG_URL.parse().unwrap(),
        default_options(),
        &env.installer_proxy(),
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
async fn rejects_zbi_v1() {
    let env = TestEnv::builder().build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("update-mode", &force_recovery_json())
        .add_file("bootloader", "new bootloader")
        .add_file("zbi", "fake zbi");

    let result = env.run_update().await;
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
            Paver(PaverEvent::QueryConfigurationStatus { configuration: paver::Configuration::A }),
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::B
            }),
            Paver(PaverEvent::BootManagerFlush),
            PackageResolve(UPDATE_PKG_URL.to_string()),
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn rejects_zbi() {
    let env = TestEnv::builder().build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", make_images_json_zbi())
        .add_file("update-mode", &force_recovery_json());

    let result = env.run_update().await;
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
            Paver(PaverEvent::QueryConfigurationStatus { configuration: paver::Configuration::A }),
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::B
            }),
            Paver(PaverEvent::BootManagerFlush),
            PackageResolve(UPDATE_PKG_URL.to_string()),
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn rejects_skip_recovery_flag() {
    let env = TestEnv::builder().build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages", make_packages_json([]))
        .add_file("update-mode", &force_recovery_json());

    let result = env
        .run_update_with_options(
            UPDATE_PKG_URL,
            Options {
                initiator: Initiator::User,
                allow_attach_to_existing_attempt: true,
                should_write_recovery: false,
            },
        )
        .await;
    assert!(result.is_err(), "system updater succeeded when it should fail");
}
