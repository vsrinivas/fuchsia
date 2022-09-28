// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::*, pretty_assertions::assert_eq};

#[fasync::run_singlethreaded(test)]
async fn fails_setting_configuration_active_v1() {
    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder.insert_hook(mphooks::return_error(|event| match event {
                PaverEvent::SetConfigurationActive { .. } => Status::INTERNAL,
                _ => Status::OK,
            }))
        })
        .build()
        .await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("zbi", "fake zbi");

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator:
                metrics::OtaResultAttemptsMigratedMetricDimensionInitiator::UserInitiatedCheck
                    as u32,
            phase: metrics::OtaResultAttemptsMigratedMetricDimensionPhase::ImageCommit as u32,
            status_code: metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode::Error as u32,
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
                configuration: paver::Configuration::B,
                asset: paver::Asset::Kernel,
                payload: b"fake zbi".to_vec(),
            }),
            Paver(PaverEvent::DataSinkFlush),
            ReplaceRetainedPackages(vec![]),
            Gc,
            BlobfsSync,
            Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn fails_commit_recovery_v1() {
    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder.insert_hook(mphooks::return_error(|event| match event {
                PaverEvent::SetConfigurationUnbootable { configuration } => match configuration {
                    paver::Configuration::A => Status::INTERNAL,
                    _ => Status::OK,
                },
                _ => Status::OK,
            }))
        })
        .build()
        .await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("update-mode", &force_recovery_json())
        .add_file("recovery", "the recovery image")
        .add_file("recovery.vbmeta", "the recovery vbmeta");

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator:
                metrics::OtaResultAttemptsMigratedMetricDimensionInitiator::UserInitiatedCheck
                    as u32,
            phase: metrics::OtaResultAttemptsMigratedMetricDimensionPhase::ImageCommit as u32,
            status_code: metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode::Error as u32,
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
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn fails_setting_configuration_active() {
    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder.insert_hook(mphooks::return_error(|event| match event {
                PaverEvent::SetConfigurationActive { .. } => Status::INTERNAL,
                _ => Status::OK,
            }))
        })
        .build()
        .await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("images.json", make_images_json_zbi())
        .add_file("epoch.json", make_current_epoch_json());

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator:
                metrics::OtaResultAttemptsMigratedMetricDimensionInitiator::UserInitiatedCheck
                    as u32,
            phase: metrics::OtaResultAttemptsMigratedMetricDimensionPhase::ImageCommit as u32,
            status_code: metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode::Error as u32,
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
                configuration: paver::Configuration::B,
                asset: paver::Asset::Kernel,
            }),
            Paver(PaverEvent::DataSinkFlush),
            ReplaceRetainedPackages(vec![]),
            Gc,
            BlobfsSync,
            Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn fails_commit_recovery() {
    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder.insert_hook(mphooks::return_error(|event| match event {
                PaverEvent::SetConfigurationUnbootable { configuration } => match configuration {
                    paver::Configuration::A => Status::INTERNAL,
                    _ => Status::OK,
                },
                _ => Status::OK,
            }))
        })
        .build()
        .await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", make_images_json_recovery())
        .add_file("update-mode", &force_recovery_json());

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator:
                metrics::OtaResultAttemptsMigratedMetricDimensionInitiator::UserInitiatedCheck
                    as u32,
            phase: metrics::OtaResultAttemptsMigratedMetricDimensionPhase::ImageCommit as u32,
            status_code: metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode::Error as u32,
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
        ]
    );
}
