// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    fidl_fuchsia_pkg::ResolveError,
    fidl_fuchsia_update_installer_ext::{
        Progress, StageFailureReason, State, StateId, UpdateInfo, UpdateInfoAndProgress,
    },
    pretty_assertions::assert_eq,
};

#[fasync::run_singlethreaded(test)]
async fn fails_on_paver_connect_error_v1() {
    let env = TestEnv::builder().unregister_protocol(Protocol::Paver).build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake_zbi");

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    // Appmgr will close the paver service channel when it is unable to forward the channel to any
    // implementation of that protocol, but it is a race condition as to whether or not the system
    // updater will be able to send the requests to open the data sink and boot manager connections
    // before that happens. So, the update attempt will either fail very early or when it attempts
    // to query the current configuration.
    let interactions = env.take_interactions();
    assert!(
        interactions == &[]
            || interactions == &[Gc, PackageResolve(UPDATE_PKG_URL.to_string()), Gc, BlobfsSync,],
        "expected early failure or failure while querying current configuration. Got {:#?}",
        interactions
    );
}

#[fasync::run_singlethreaded(test)]
async fn fails_on_paver_connect_error() {
    let env = TestEnv::builder().unregister_protocol(Protocol::Paver).build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("images.json", make_images_json_zbi());

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    // Appmgr will close the paver service channel when it is unable to forward the channel to any
    // implementation of that protocol, but it is a race condition as to whether or not the system
    // updater will be able to send the requests to open the data sink and boot manager connections
    // before that happens. So, the update attempt will either fail very early or when it attempts
    // to query the current configuration.
    let interactions = env.take_interactions();
    assert!(
        interactions == &[]
            || interactions == &[Gc, PackageResolve(UPDATE_PKG_URL.to_string()), Gc, BlobfsSync,],
        "expected early failure or failure while querying current configuration. Got {:#?}",
        interactions
    );
}

#[fasync::run_singlethreaded(test)]
async fn fails_on_missing_zbi_error() {
    let env = TestEnv::builder().build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", make_images_json_recovery());

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator:
                metrics::OtaResultAttemptsMigratedMetricDimensionInitiator::UserInitiatedCheck
                    as u32,
            phase: metrics::OtaResultAttemptsMigratedMetricDimensionPhase::Tufupdate as u32,
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
            },),
            Paver(PaverEvent::ReadAsset {
                configuration: paver::Configuration::A,
                asset: paver::Asset::Kernel
            },),
            Paver(PaverEvent::QueryCurrentConfiguration,),
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
async fn fails_on_missing_zbi_error_v1() {
    let env = TestEnv::builder().build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json());

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator:
                metrics::OtaResultAttemptsMigratedMetricDimensionInitiator::UserInitiatedCheck
                    as u32,
            phase: metrics::OtaResultAttemptsMigratedMetricDimensionPhase::ImageWrite as u32,
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
            },),
            Paver(PaverEvent::ReadAsset {
                configuration: paver::Configuration::A,
                asset: paver::Asset::Kernel
            },),
            Paver(PaverEvent::QueryCurrentConfiguration,),
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
async fn fails_on_image_write_error_v1() {
    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder.insert_hook(mphooks::return_error(|event| match event {
                PaverEvent::WriteAsset { .. } => Status::INTERNAL,
                _ => Status::OK,
            }))
        })
        .build()
        .await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("zbi", "fake_zbi");

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator:
                metrics::OtaResultAttemptsMigratedMetricDimensionInitiator::UserInitiatedCheck
                    as u32,
            phase: metrics::OtaResultAttemptsMigratedMetricDimensionPhase::ImageWrite as u32,
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
            },),
            Paver(PaverEvent::ReadAsset {
                configuration: paver::Configuration::A,
                asset: paver::Asset::Kernel
            },),
            Paver(PaverEvent::QueryCurrentConfiguration,),
            Paver(PaverEvent::QueryConfigurationStatus { configuration: paver::Configuration::A }),
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::B
            }),
            Paver(PaverEvent::BootManagerFlush),
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::B,
                asset: paver::Asset::Kernel,
                payload: b"fake_zbi".to_vec(),
            },),
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn fails_on_image_write_error() {
    let images_json = serde_json::to_string(
        &::update_package::ImagePackagesManifest::builder()
            .fuchsia_package(
                ::update_package::ImageMetadata::new(
                    5,
                    hash(8),
                    image_package_resource_url("update-images-fuchsia", 9, "zbi"),
                ),
                None,
            )
            .clone()
            .build(),
    )
    .unwrap();
    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder.insert_hook(mphooks::return_error(|event| match event {
                PaverEvent::WriteAsset { .. } => Status::INTERNAL,
                _ => Status::OK,
            }))
        })
        .build()
        .await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", images_json);

    env.resolver
        .url(image_package_url_to_string("update-images-fuchsia", 9))
        .resolve(&env.resolver.package("fuchsia", hashstr(8)).add_file("zbi", "zbi zbi"));

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator:
                metrics::OtaResultAttemptsMigratedMetricDimensionInitiator::UserInitiatedCheck
                    as u32,
            phase: metrics::OtaResultAttemptsMigratedMetricDimensionPhase::ImageWrite as u32,
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
            },),
            Paver(PaverEvent::ReadAsset {
                configuration: paver::Configuration::A,
                asset: paver::Asset::Kernel
            },),
            Paver(PaverEvent::QueryCurrentConfiguration,),
            Paver(PaverEvent::QueryConfigurationStatus { configuration: paver::Configuration::A }),
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::B
            }),
            Paver(PaverEvent::BootManagerFlush),
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Paver(PaverEvent::ReadAsset {
                configuration: paver::Configuration::B,
                asset: paver::Asset::Kernel,
            },),
            ReplaceRetainedPackages(vec![hash(9).into()]),
            Gc,
            PackageResolve(image_package_url_to_string("update-images-fuchsia", 9)),
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::B,
                asset: paver::Asset::Kernel,
                payload: b"zbi zbi".to_vec(),
            },),
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn skip_recovery_does_not_write_recovery_or_vbmeta_v1() {
    let env = TestEnv::builder().build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("zbi", "fake zbi")
        .add_file("recovery", "new recovery")
        .add_file("recovery.vbmeta", "new recovery vbmeta");

    env.run_update_with_options(
        UPDATE_PKG_URL,
        Options { should_write_recovery: false, ..default_options() },
    )
    .await
    .expect("success");

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
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn skip_recovery_does_not_write_recovery_or_vbmeta() {
    let images_json = serde_json::to_string(
        &::update_package::ImagePackagesManifest::builder()
            .fuchsia_package(
                ::update_package::ImageMetadata::new(
                    0,
                    Hash::from_str(EMPTY_HASH).unwrap(),
                    image_package_resource_url("update-images-fuchsia", 9, "zbi"),
                ),
                None,
            )
            .recovery_package(
                ::update_package::ImageMetadata::new(
                    2,
                    hash(4),
                    image_package_resource_url("update-images-recovery", 9, "rzbi"),
                ),
                None,
            )
            .clone()
            .build(),
    )
    .unwrap();

    let env = TestEnv::builder().build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", images_json);

    env.run_update_with_options(
        UPDATE_PKG_URL,
        Options { should_write_recovery: false, ..default_options() },
    )
    .await
    .expect("success");

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
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn writes_to_both_configs_if_abr_not_supported_v1() {
    let env = TestEnv::builder()
        .paver_service(|builder| builder.boot_manager_close_with_epitaph(Status::NOT_SUPPORTED))
        .build()
        .await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("zbi", "fake_zbi");

    env.run_update().await.expect("success");

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
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::A,
                asset: paver::Asset::Kernel,
                payload: b"fake_zbi".to_vec(),
            }),
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::B,
                asset: paver::Asset::Kernel,
                payload: b"fake_zbi".to_vec(),
            }),
            Paver(PaverEvent::DataSinkFlush),
            ReplaceRetainedPackages(vec![]),
            Gc,
            BlobfsSync,
            Reboot,
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn writes_to_both_configs_if_abr_not_supported() {
    let images_json = serde_json::to_string(
        &::update_package::ImagePackagesManifest::builder()
            .fuchsia_package(
                ::update_package::ImageMetadata::new(
                    5,
                    hash(8),
                    image_package_resource_url("update-images-fuchsia", 9, "zbi"),
                ),
                None,
            )
            .clone()
            .build(),
    )
    .unwrap();

    let env = TestEnv::builder()
        .paver_service(|builder| builder.boot_manager_close_with_epitaph(Status::NOT_SUPPORTED))
        .build()
        .await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", images_json);

    env.resolver
        .url(image_package_url_to_string("update-images-fuchsia", 9))
        .resolve(&env.resolver.package("fuchsia", hashstr(8)).add_file("zbi", "zbi zbi"));

    env.run_update().await.expect("success");

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
            PackageResolve(UPDATE_PKG_URL.to_string()),
            ReplaceRetainedPackages(vec![hash(9).into()]),
            Gc,
            PackageResolve(image_package_url_to_string("update-images-fuchsia", 9)),
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::A,
                asset: paver::Asset::Kernel,
                payload: b"zbi zbi".to_vec(),
            }),
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::B,
                asset: paver::Asset::Kernel,
                payload: b"zbi zbi".to_vec(),
            }),
            Paver(PaverEvent::DataSinkFlush),
            ReplaceRetainedPackages(vec![]),
            Gc,
            BlobfsSync,
            Reboot,
        ]
    );
}

#[fasync::run_singlethreaded(test)]
// If the current partition isn't healthy, the system-updater aborts.
async fn does_not_update_with_unhealthy_current_partition_v1() {
    let current_config = paver::Configuration::A;

    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Pending)))
                .current_config(current_config)
        })
        .build()
        .await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake_zbi");

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator:
                metrics::OtaResultAttemptsMigratedMetricDimensionInitiator::UserInitiatedCheck
                    as u32,
            phase: metrics::OtaResultAttemptsMigratedMetricDimensionPhase::Tufupdate as u32,
            status_code: metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode::Error as u32,
        }
    );

    assert_eq!(
        env.take_interactions(),
        vec![
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::ReadAsset {
                configuration: current_config,
                asset: paver::Asset::VerifiedBootMetadata
            }),
            Paver(PaverEvent::ReadAsset {
                configuration: current_config,
                asset: paver::Asset::Kernel
            }),
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::QueryConfigurationStatus { configuration: current_config }),
        ]
    );
}

#[fasync::run_singlethreaded(test)]
// If the current partition isn't healthy, the system-updater aborts.
async fn does_not_update_with_unhealthy_current_partition() {
    let current_config = paver::Configuration::A;

    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Pending)))
                .current_config(current_config)
        })
        .build()
        .await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("images.json", make_images_json_zbi());

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator:
                metrics::OtaResultAttemptsMigratedMetricDimensionInitiator::UserInitiatedCheck
                    as u32,
            phase: metrics::OtaResultAttemptsMigratedMetricDimensionPhase::Tufupdate as u32,
            status_code: metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode::Error as u32,
        }
    );

    assert_eq!(
        env.take_interactions(),
        vec![
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::ReadAsset {
                configuration: current_config,
                asset: paver::Asset::VerifiedBootMetadata
            }),
            Paver(PaverEvent::ReadAsset {
                configuration: current_config,
                asset: paver::Asset::Kernel
            }),
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::QueryConfigurationStatus { configuration: current_config }),
        ]
    );
}

// If the alternate configuration can't be marked unbootable, the system-updater fails.
#[fasync::run_singlethreaded(test)]
async fn does_not_update_if_alternate_cant_be_marked_unbootable_v1() {
    let current_config = paver::Configuration::A;

    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder
                .insert_hook(mphooks::return_error(|event| match event {
                    PaverEvent::SetConfigurationUnbootable { .. } => Status::INTERNAL,
                    _ => Status::OK,
                }))
                .current_config(current_config)
        })
        .build()
        .await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake_zbi");

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator:
                metrics::OtaResultAttemptsMigratedMetricDimensionInitiator::UserInitiatedCheck
                    as u32,
            phase: metrics::OtaResultAttemptsMigratedMetricDimensionPhase::Tufupdate as u32,
            status_code: metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode::Error as u32,
        }
    );

    assert_eq!(
        env.take_interactions(),
        vec![
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::ReadAsset {
                configuration: current_config,
                asset: paver::Asset::VerifiedBootMetadata
            }),
            Paver(PaverEvent::ReadAsset {
                configuration: current_config,
                asset: paver::Asset::Kernel
            }),
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::QueryConfigurationStatus { configuration: current_config }),
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::B
            }),
            // Make sure we flush, even if marking Unbootable failed.
            Paver(PaverEvent::BootManagerFlush),
        ]
    );
}

// If the alternate configuration can't be marked unbootable, the system-updater fails.
#[fasync::run_singlethreaded(test)]
async fn does_not_update_if_alternate_cant_be_marked_unbootable() {
    let current_config = paver::Configuration::A;

    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder
                .insert_hook(mphooks::return_error(|event| match event {
                    PaverEvent::SetConfigurationUnbootable { .. } => Status::INTERNAL,
                    _ => Status::OK,
                }))
                .current_config(current_config)
        })
        .build()
        .await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("images.json", make_images_json_zbi());

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator:
                metrics::OtaResultAttemptsMigratedMetricDimensionInitiator::UserInitiatedCheck
                    as u32,
            phase: metrics::OtaResultAttemptsMigratedMetricDimensionPhase::Tufupdate as u32,
            status_code: metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode::Error as u32,
        }
    );

    assert_eq!(
        env.take_interactions(),
        vec![
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::ReadAsset {
                configuration: current_config,
                asset: paver::Asset::VerifiedBootMetadata
            }),
            Paver(PaverEvent::ReadAsset {
                configuration: current_config,
                asset: paver::Asset::Kernel
            }),
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::QueryConfigurationStatus { configuration: current_config }),
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::B
            }),
            // Make sure we flush, even if marking Unbootable failed.
            Paver(PaverEvent::BootManagerFlush),
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn writes_to_b_if_abr_supported_and_current_config_a_v1() {
    assert_writes_for_current_and_target_v1(paver::Configuration::A, paver::Configuration::B).await
}

#[fasync::run_singlethreaded(test)]
async fn writes_to_a_if_abr_supported_and_current_config_b_v1() {
    assert_writes_for_current_and_target_v1(paver::Configuration::B, paver::Configuration::A).await
}

#[fasync::run_singlethreaded(test)]
async fn writes_to_a_if_abr_supported_and_current_config_r_v1() {
    assert_writes_for_current_and_target_v1(paver::Configuration::Recovery, paver::Configuration::A)
        .await
}

#[fasync::run_singlethreaded(test)]
async fn writes_to_b_if_abr_supported_and_current_config_a() {
    assert_writes_for_current_and_target(paver::Configuration::A, paver::Configuration::B).await
}

#[fasync::run_singlethreaded(test)]
async fn writes_to_a_if_abr_supported_and_current_config_b() {
    assert_writes_for_current_and_target(paver::Configuration::B, paver::Configuration::A).await
}

#[fasync::run_singlethreaded(test)]
async fn writes_to_a_if_abr_supported_and_current_config_r() {
    assert_writes_for_current_and_target(paver::Configuration::Recovery, paver::Configuration::A)
        .await
}

#[fasync::run_singlethreaded(test)]
async fn writes_recovery_v1() {
    let env = TestEnv::builder().build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("zbi", "fake zbi")
        .add_file("recovery", "new recovery");

    env.run_update().await.expect("success");

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
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::Recovery,
                asset: paver::Asset::Kernel,
                payload: b"new recovery".to_vec(),
            }),
            Paver(PaverEvent::DataSinkFlush),
            ReplaceRetainedPackages(vec![]),
            Gc,
            BlobfsSync,
            Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn writes_recovery_vbmeta_v1() {
    let env = TestEnv::builder().build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("zbi", "fake zbi")
        .add_file("recovery", "new recovery")
        .add_file("recovery.vbmeta", "new recovery vbmeta");

    env.run_update().await.expect("success");

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
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::Recovery,
                asset: paver::Asset::Kernel,
                payload: b"new recovery".to_vec(),
            }),
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::Recovery,
                asset: paver::Asset::VerifiedBootMetadata,
                payload: b"new recovery vbmeta".to_vec(),
            }),
            Paver(PaverEvent::DataSinkFlush),
            ReplaceRetainedPackages(vec![]),
            Gc,
            BlobfsSync,
            Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn writes_fuchsia_vbmeta_v1() {
    let env = TestEnv::builder().build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("zbi", "fake zbi")
        .add_file("fuchsia.vbmeta", "fake zbi vbmeta");

    env.run_update().await.expect("success");

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
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::B,
                asset: paver::Asset::VerifiedBootMetadata,
                payload: b"fake zbi vbmeta".to_vec(),
            }),
            Paver(PaverEvent::DataSinkFlush),
            ReplaceRetainedPackages(vec![]),
            Gc,
            BlobfsSync,
            Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}

// Run an update with a given current config, assert that it succeeded, and return its interactions.
async fn update_with_current_config_v1(
    current_config: paver::Configuration,
) -> Vec<SystemUpdaterInteraction> {
    let env = TestEnv::builder()
        .paver_service(|builder| builder.current_config(current_config))
        .build()
        .await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("zbi", "fake_zbi");

    env.run_update().await.expect("success");

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

    env.take_interactions()
}

// Run an update with a given current config, assert that it succeeded, and return its interactions.
async fn update_with_current_config(
    current_config: paver::Configuration,
) -> Vec<SystemUpdaterInteraction> {
    let images_json = ::update_package::ImagePackagesManifest::builder()
        .fuchsia_package(
            ::update_package::ImageMetadata::new(
                5,
                hash(2),
                image_package_resource_url("update-images-fuchsia", 9, "zbi"),
            ),
            None,
        )
        .clone()
        .build();
    let env = TestEnv::builder()
        .paver_service(|builder| builder.current_config(current_config))
        .build()
        .await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", serde_json::to_string(&images_json).unwrap());

    env.resolver
        .url(image_package_url_to_string("update-images-fuchsia", 9))
        .resolve(&env.resolver.package("zbi", hashstr(7)).add_file("zbi", "zbi contents"));

    env.run_update().await.expect("success");

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

    env.take_interactions()
}

// Asserts that we have a "normal" update flow that targets `target_config`, when `current_config`
// is the current configuration.
async fn assert_writes_for_current_and_target_v1(
    current_config: paver::Configuration,
    target_config: paver::Configuration,
) {
    assert_eq!(
        update_with_current_config_v1(current_config).await,
        vec![
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::ReadAsset {
                configuration: current_config,
                asset: paver::Asset::VerifiedBootMetadata
            }),
            Paver(PaverEvent::ReadAsset {
                configuration: current_config,
                asset: paver::Asset::Kernel
            }),
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::QueryConfigurationStatus { configuration: current_config }),
            Paver(PaverEvent::SetConfigurationUnbootable { configuration: target_config }),
            Paver(PaverEvent::BootManagerFlush),
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Paver(PaverEvent::WriteAsset {
                configuration: target_config,
                asset: paver::Asset::Kernel,
                payload: b"fake_zbi".to_vec(),
            }),
            Paver(PaverEvent::DataSinkFlush),
            ReplaceRetainedPackages(vec![]),
            Gc,
            BlobfsSync,
            Paver(PaverEvent::SetConfigurationActive { configuration: target_config }),
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}

// Asserts that we have a "normal" update flow that targets `target_config`, when `current_config`
// is the current configuration.
async fn assert_writes_for_current_and_target(
    current_config: paver::Configuration,
    target_config: paver::Configuration,
) {
    assert_eq!(
        update_with_current_config(current_config).await,
        vec![
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::ReadAsset {
                configuration: current_config,
                asset: paver::Asset::VerifiedBootMetadata
            }),
            Paver(PaverEvent::ReadAsset {
                configuration: current_config,
                asset: paver::Asset::Kernel
            }),
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::QueryConfigurationStatus { configuration: current_config }),
            Paver(PaverEvent::SetConfigurationUnbootable { configuration: target_config }),
            Paver(PaverEvent::BootManagerFlush),
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Paver(PaverEvent::ReadAsset {
                configuration: target_config,
                asset: paver::Asset::Kernel,
            }),
            ReplaceRetainedPackages(vec![hash(9).into()]),
            Gc,
            PackageResolve(image_package_url_to_string("update-images-fuchsia", 9)),
            Paver(PaverEvent::WriteAsset {
                configuration: target_config,
                asset: paver::Asset::Kernel,
                payload: b"zbi contents".to_vec()
            }),
            Paver(PaverEvent::DataSinkFlush),
            ReplaceRetainedPackages(vec![]),
            Gc,
            BlobfsSync,
            Paver(PaverEvent::SetConfigurationActive { configuration: target_config }),
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}

/// Verifies that when we fail to resolve images, we get a Stage failure with the
/// expected `StageFailureReason`.
async fn assert_stage_resolve_failure_reason(
    resolve_error: fidl_fuchsia_pkg::ResolveError,
    expected_reason: StageFailureReason,
) {
    let images_json = ::update_package::ImagePackagesManifest::builder()
        .fuchsia_package(
            ::update_package::ImageMetadata::new(
                5,
                hash(2),
                image_package_resource_url("update-images-fuchsia", 9, "zbi"),
            ),
            None,
        )
        .clone()
        .build();

    let env = TestEnv::builder().build().await;

    // ResolveError is only raised if images.json is present.
    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", serde_json::to_string(&images_json).unwrap());

    env.resolver.mock_resolve_failure(
        image_package_url_to_string("update-images-fuchsia", 9),
        resolve_error,
    );

    let mut attempt = env.start_update().await.unwrap();

    let info = UpdateInfo::builder().download_size(0).build();
    let progress = Progress::builder().fraction_completed(0.0).bytes_downloaded(0).build();
    assert_eq!(attempt.next().await.unwrap().unwrap(), State::Prepare);
    assert_eq!(
        attempt.next().await.unwrap().unwrap(),
        State::Stage(
            UpdateInfoAndProgress::builder()
                .info(info.clone())
                .progress(Progress::builder().fraction_completed(0.0).bytes_downloaded(0).build())
                .build()
        )
    );
    assert_eq!(
        attempt.next().await.unwrap().unwrap(),
        State::FailStage(
            UpdateInfoAndProgress::builder()
                .info(info)
                .progress(progress)
                .build()
                .with_stage_reason(expected_reason)
        )
    );
}

#[fasync::run_singlethreaded(test)]
async fn stage_failure_reason_out_of_space() {
    assert_stage_resolve_failure_reason(
        fidl_fuchsia_pkg::ResolveError::NoSpace,
        StageFailureReason::OutOfSpace,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn stage_failure_reason_internal() {
    assert_stage_resolve_failure_reason(
        fidl_fuchsia_pkg::ResolveError::AccessDenied,
        StageFailureReason::Internal,
    )
    .await;
    assert_stage_resolve_failure_reason(
        fidl_fuchsia_pkg::ResolveError::RepoNotFound,
        StageFailureReason::Internal,
    )
    .await;
    assert_stage_resolve_failure_reason(
        fidl_fuchsia_pkg::ResolveError::Internal,
        StageFailureReason::Internal,
    )
    .await;
    assert_stage_resolve_failure_reason(
        fidl_fuchsia_pkg::ResolveError::Io,
        StageFailureReason::Internal,
    )
    .await;
    assert_stage_resolve_failure_reason(
        fidl_fuchsia_pkg::ResolveError::PackageNotFound,
        StageFailureReason::Internal,
    )
    .await;
    assert_stage_resolve_failure_reason(
        fidl_fuchsia_pkg::ResolveError::UnavailableBlob,
        StageFailureReason::Internal,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn retry_image_package_resolve_once() {
    let images_json = ::update_package::ImagePackagesManifest::builder()
        .fuchsia_package(
            ::update_package::ImageMetadata::new(
                5,
                hash(2),
                image_package_resource_url("update-images-fuchsia", 9, "zbi"),
            ),
            None,
        )
        .clone()
        .build();

    let env = TestEnv::builder().build().await;

    let base_package = "fuchsia-pkg://fuchsia.com/system_image/0?hash=beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead";

    env.resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([base_package]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", serde_json::to_string(&images_json).unwrap());

    env.resolver
        .url(base_package)
        .resolve(&env.resolver.package("deadbeef", hashstr(7)).add_file("beef", "dead"));

    env.resolver.url(image_package_url_to_string("update-images-fuchsia", 9)).respond_serially(
        vec![
            Err(ResolveError::NoSpace),
            Ok(env.resolver.package("zbi", hashstr(8)).add_file("zbi", "real zbi contents")),
        ],
    );

    env.run_update_with_options("fuchsia-pkg://fuchsia.com/another-update/4", default_options())
        .await
        .expect("run system updater");

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
            PackageResolve("fuchsia-pkg://fuchsia.com/another-update/4".to_string()),
            Paver(PaverEvent::ReadAsset {
                configuration: paver::Configuration::B,
                asset: paver::Asset::Kernel,
            }),
            // Verify that base packages are removed from the retained package index if
            // image fails to resolve with OutOfSpace.
            ReplaceRetainedPackages(vec![
                "beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead".parse().unwrap(),
                hash(9).into(),
            ]),
            Gc,
            PackageResolve(image_package_url_to_string("update-images-fuchsia", 9)),
            ReplaceRetainedPackages(vec![hash(9).into()]),
            Gc,
            PackageResolve(image_package_url_to_string("update-images-fuchsia", 9)),
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::B,
                asset: paver::Asset::Kernel,
                payload: b"real zbi contents".to_vec()
            }),
            Paver(PaverEvent::DataSinkFlush),
            ReplaceRetainedPackages(vec![
                "beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead".parse().unwrap(),
            ]),
            Gc,
            PackageResolve(base_package.to_string()),
            BlobfsSync,
            Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn retry_image_package_resolve_twice_fails_update() {
    let images_json = ::update_package::ImagePackagesManifest::builder()
        .fuchsia_package(
            ::update_package::ImageMetadata::new(
                5,
                hash(2),
                image_package_resource_url("update-images-fuchsia", 9, "zbi"),
            ),
            None,
        )
        .clone()
        .build();

    let env = TestEnv::builder().build().await;

    let base_package = "fuchsia-pkg://fuchsia.com/system_image/0?hash=beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead";

    env.resolver
        .url(base_package)
        .resolve(&env.resolver.package("deadbeef", hashstr(7)).add_file("beef", "dead"));

    env.resolver.url(image_package_url_to_string("update-images-fuchsia", 9)).respond_serially(
        vec![
            Err(ResolveError::NoSpace),
            Err(ResolveError::NoSpace),
            Ok(env.resolver.package("zbi", hashstr(8)).add_file("zbi", "real zbi contents")),
        ],
    );

    env.resolver.url(UPDATE_PKG_URL).resolve(
        &env.resolver
            .package("update", "upd4t3")
            .add_file("packages.json", make_packages_json([base_package]))
            .add_file("epoch.json", make_current_epoch_json())
            .add_file("images.json", serde_json::to_string(&images_json).unwrap()),
    );

    let mut attempt = env.start_update().await.unwrap();

    let info = UpdateInfo::builder().download_size(0).build();
    let progress = Progress::builder().fraction_completed(0.0).bytes_downloaded(0).build();

    assert_eq!(attempt.next().await.unwrap().unwrap(), State::Prepare);

    assert_eq!(attempt.next().await.unwrap().unwrap().id(), StateId::Stage);

    assert_eq!(
        attempt.next().await.unwrap().unwrap(),
        State::FailStage(
            UpdateInfoAndProgress::builder()
                .info(info)
                .progress(progress)
                .build()
                .with_stage_reason(StageFailureReason::OutOfSpace)
        )
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
            // Verify that base packages are removed from the retained package index if
            // image fails to resolve with OutOfSpace.
            ReplaceRetainedPackages(vec![
                "beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead".parse().unwrap(),
                hash(9).into(),
            ]),
            Gc,
            PackageResolve(image_package_url_to_string("update-images-fuchsia", 9)),
            ReplaceRetainedPackages(vec![hash(9).into()]),
            Gc,
            PackageResolve(image_package_url_to_string("update-images-fuchsia", 9)),
        ]
    );
}

fn construct_events(middle: &mut Vec<SystemUpdaterInteraction>) -> Vec<SystemUpdaterInteraction> {
    let mut preamble = vec![
        Paver(PaverEvent::QueryCurrentConfiguration),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::A,
            asset: paver::Asset::VerifiedBootMetadata,
        }),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::A,
            asset: paver::Asset::Kernel,
        }),
        Paver(PaverEvent::QueryCurrentConfiguration),
        Paver(PaverEvent::QueryConfigurationStatus { configuration: paver::Configuration::A }),
        Paver(PaverEvent::SetConfigurationUnbootable { configuration: paver::Configuration::B }),
        Paver(PaverEvent::BootManagerFlush),
        PackageResolve("fuchsia-pkg://fuchsia.com/another-update/4".to_string()),
    ];

    let mut postscript = vec![
        Paver(PaverEvent::DataSinkFlush),
        ReplaceRetainedPackages(vec![]),
        Gc,
        BlobfsSync,
        Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
        Paver(PaverEvent::BootManagerFlush),
        Reboot,
    ];

    preamble.append(middle);
    preamble.append(&mut postscript);
    preamble
}

#[fasync::run_singlethreaded(test)]
async fn writes_fuchsia() {
    let images_json = ::update_package::ImagePackagesManifest::builder()
        .fuchsia_package(
            ::update_package::ImageMetadata::new(
                5,
                hash(2),
                image_package_resource_url("update-images-fuchsia", 9, "zbi"),
            ),
            None,
        )
        .clone()
        .build();

    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder.insert_hook(mphooks::read_asset(|configuration, asset| {
                match (configuration, asset) {
                    (paver::Configuration::A, paver::Asset::Kernel) => {
                        Ok(b"not the right zbi".to_vec())
                    }
                    (paver::Configuration::B, paver::Asset::Kernel) => Ok(b"bad zbi".to_vec()),
                    (_, _) => Ok(vec![]),
                }
            }))
        })
        .build()
        .await;

    env.resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", serde_json::to_string(&images_json).unwrap());

    env.resolver
        .url(image_package_url_to_string("update-images-fuchsia", 9))
        .resolve(&env.resolver.package("zbi", hashstr(7)).add_file("zbi", "zbi contents"));

    env.run_update_with_options("fuchsia-pkg://fuchsia.com/another-update/4", default_options())
        .await
        .expect("run system updater");

    let events = vec![
        Paver(PaverEvent::QueryCurrentConfiguration),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::A,
            asset: paver::Asset::VerifiedBootMetadata,
        }),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::A,
            asset: paver::Asset::Kernel,
        }),
        Paver(PaverEvent::QueryCurrentConfiguration),
        Paver(PaverEvent::QueryConfigurationStatus { configuration: paver::Configuration::A }),
        Paver(PaverEvent::SetConfigurationUnbootable { configuration: paver::Configuration::B }),
        Paver(PaverEvent::BootManagerFlush),
        PackageResolve("fuchsia-pkg://fuchsia.com/another-update/4".to_string()),
        // Check that we read from both configurations and write resolved zbi contents
        // to desired configuration.
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::B,
            asset: paver::Asset::Kernel,
        }),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::A,
            asset: paver::Asset::Kernel,
        }),
        ReplaceRetainedPackages(vec![hash(9).into()]),
        Gc,
        PackageResolve(image_package_url_to_string("update-images-fuchsia", 9)),
        Paver(PaverEvent::WriteAsset {
            configuration: paver::Configuration::B,
            asset: paver::Asset::Kernel,
            payload: b"zbi contents".to_vec(),
        }),
        // Rest of update flow.
        Paver(PaverEvent::DataSinkFlush),
        ReplaceRetainedPackages(vec![]),
        Gc,
        BlobfsSync,
        Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
        Paver(PaverEvent::BootManagerFlush),
        Reboot,
    ];

    assert_eq!(env.take_interactions(), events);
}

#[fasync::run_singlethreaded(test)]
async fn writes_fuchsia_vbmeta() {
    let images_json = ::update_package::ImagePackagesManifest::builder()
        .fuchsia_package(
            ::update_package::ImageMetadata::new(
                5,
                hash(2),
                image_package_resource_url("update-images-fuchsia", 9, "zbi"),
            ),
            Some(::update_package::ImageMetadata::new(
                5,
                hash(1),
                image_package_resource_url("update-images-fuchsia", 9, "vbmeta"),
            )),
        )
        .clone()
        .build();

    let env = TestEnv::builder().build().await;

    env.resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", serde_json::to_string(&images_json).unwrap());

    env.resolver.url(image_package_url_to_string("update-images-fuchsia", 9)).resolve(
        &env.resolver
            .package("zbi", hashstr(7))
            .add_file("zbi", "zbi contents")
            .add_file("vbmeta", "vbmeta contents"),
    );

    env.run_update_with_options("fuchsia-pkg://fuchsia.com/another-update/4", default_options())
        .await
        .expect("run system updater");

    let events = vec![
        Paver(PaverEvent::QueryCurrentConfiguration),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::A,
            asset: paver::Asset::VerifiedBootMetadata,
        }),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::A,
            asset: paver::Asset::Kernel,
        }),
        Paver(PaverEvent::QueryCurrentConfiguration),
        Paver(PaverEvent::QueryConfigurationStatus { configuration: paver::Configuration::A }),
        Paver(PaverEvent::SetConfigurationUnbootable { configuration: paver::Configuration::B }),
        Paver(PaverEvent::BootManagerFlush),
        PackageResolve("fuchsia-pkg://fuchsia.com/another-update/4".to_string()),
        // Events we care about.
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::B,
            asset: paver::Asset::Kernel,
        }),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::B,
            asset: paver::Asset::VerifiedBootMetadata,
        }),
        ReplaceRetainedPackages(vec![hash(9).into()]),
        Gc,
        PackageResolve(image_package_url_to_string("update-images-fuchsia", 9)),
        Paver(PaverEvent::WriteAsset {
            configuration: paver::Configuration::B,
            asset: paver::Asset::Kernel,
            payload: b"zbi contents".to_vec(),
        }),
        Paver(PaverEvent::WriteAsset {
            configuration: paver::Configuration::B,
            asset: paver::Asset::VerifiedBootMetadata,
            payload: b"vbmeta contents".to_vec(),
        }),
        // Rest of update flow.
        Paver(PaverEvent::DataSinkFlush),
        ReplaceRetainedPackages(vec![]),
        Gc,
        BlobfsSync,
        Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
        Paver(PaverEvent::BootManagerFlush),
        Reboot,
    ];

    assert_eq!(env.take_interactions(), events);
}

#[fasync::run_singlethreaded(test)]
async fn zbi_match_in_desired_config() {
    let images_json = ::update_package::ImagePackagesManifest::builder()
        .fuchsia_package(
            ::update_package::ImageMetadata::new(
                8,
                Hash::from_str(MATCHING_HASH).unwrap(),
                image_package_resource_url("update-images-fuchsia", 9, "zbi"),
            ),
            None,
        )
        .clone()
        .build();

    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder.insert_hook(mphooks::read_asset(|configuration, asset| {
                match (configuration, asset) {
                    (paver::Configuration::B, paver::Asset::Kernel) => Ok(b"matching".to_vec()),
                    (_, _) => Ok(vec![]),
                }
            }))
        })
        .build()
        .await;

    env.resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", serde_json::to_string(&images_json).unwrap());

    env.run_update_with_options("fuchsia-pkg://fuchsia.com/another-update/4", default_options())
        .await
        .expect("run system updater");

    let mut events = vec![Paver(PaverEvent::ReadAsset {
        configuration: paver::Configuration::B,
        asset: paver::Asset::Kernel,
    })];
    assert_eq!(env.take_interactions(), construct_events(&mut events));
}

#[fasync::run_singlethreaded(test)]
async fn zbi_match_in_active_config() {
    let images_json = ::update_package::ImagePackagesManifest::builder()
        .fuchsia_package(
            ::update_package::ImageMetadata::new(
                8,
                Hash::from_str(MATCHING_HASH).unwrap(),
                image_package_resource_url("update-images-fuchsia", 9, "zbi"),
            ),
            None,
        )
        .clone()
        .build();

    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder.insert_hook(mphooks::read_asset(|configuration, asset| {
                match (configuration, asset) {
                    (paver::Configuration::A, paver::Asset::Kernel) => Ok(b"matching".to_vec()),
                    (paver::Configuration::B, paver::Asset::Kernel) => {
                        Ok(b"not a match sorry".to_vec())
                    }
                    (_, _) => Ok(vec![]),
                }
            }))
        })
        .build()
        .await;

    env.resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", serde_json::to_string(&images_json).unwrap());

    env.run_update_with_options("fuchsia-pkg://fuchsia.com/another-update/4", default_options())
        .await
        .expect("run system updater");

    let mut events = vec![
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::B,
            asset: paver::Asset::Kernel,
        }),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::A,
            asset: paver::Asset::Kernel,
        }),
        Paver(PaverEvent::WriteAsset {
            configuration: paver::Configuration::B,
            asset: paver::Asset::Kernel,
            payload: b"matching".to_vec(),
        }),
    ];
    assert_eq!(env.take_interactions(), construct_events(&mut events));
}

#[fasync::run_singlethreaded(test)]
async fn zbi_match_in_active_config_error_in_desired_config() {
    let images_json = ::update_package::ImagePackagesManifest::builder()
        .fuchsia_package(
            ::update_package::ImageMetadata::new(
                8,
                Hash::from_str(MATCHING_HASH).unwrap(),
                image_package_resource_url("update-images-fuchsia", 9, "zbi"),
            ),
            None,
        )
        .clone()
        .build();

    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder.insert_hook(mphooks::read_asset(|configuration, asset| {
                match (configuration, asset) {
                    (paver::Configuration::A, paver::Asset::Kernel) => Ok(b"matching".to_vec()),
                    (paver::Configuration::B, paver::Asset::Kernel) => Ok(vec![]),
                    (_, _) => Ok(vec![]),
                }
            }))
        })
        .build()
        .await;

    env.resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", serde_json::to_string(&images_json).unwrap());

    env.resolver
        .url(image_package_url_to_string("update-images-fuchsia", 9))
        .resolve(&env.resolver.package("zbi", hashstr(8)).add_file("zbi", "real zbi contents"));

    env.run_update_with_options("fuchsia-pkg://fuchsia.com/another-update/4", default_options())
        .await
        .expect("run system updater");

    let events = vec![
        Paver(PaverEvent::QueryCurrentConfiguration),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::A,
            asset: paver::Asset::VerifiedBootMetadata,
        }),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::A,
            asset: paver::Asset::Kernel,
        }),
        Paver(PaverEvent::QueryCurrentConfiguration),
        Paver(PaverEvent::QueryConfigurationStatus { configuration: paver::Configuration::A }),
        Paver(PaverEvent::SetConfigurationUnbootable { configuration: paver::Configuration::B }),
        Paver(PaverEvent::BootManagerFlush),
        PackageResolve("fuchsia-pkg://fuchsia.com/another-update/4".to_string()),
        // system-updater raises an error so cautiously choose to resolve and write zbi
        // rather than check Configuration::A.
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::B,
            asset: paver::Asset::Kernel,
        }),
        ReplaceRetainedPackages(vec![hash(9).into()]),
        Gc,
        PackageResolve(image_package_url_to_string("update-images-fuchsia", 9)),
        Paver(PaverEvent::WriteAsset {
            configuration: paver::Configuration::B,
            asset: paver::Asset::Kernel,
            payload: b"real zbi contents".to_vec(),
        }),
        // Rest of events.
        Paver(PaverEvent::DataSinkFlush),
        ReplaceRetainedPackages(vec![]),
        Gc,
        BlobfsSync,
        Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
        Paver(PaverEvent::BootManagerFlush),
        Reboot,
    ];

    assert_eq!(env.take_interactions(), events);
}

#[fasync::run_singlethreaded(test)]
async fn recovery_already_present() {
    let images_json = ::update_package::ImagePackagesManifest::builder()
        .recovery_package(
            ::update_package::ImageMetadata::new(
                8,
                Hash::from_str(MATCHING_HASH).unwrap(),
                image_package_resource_url("update-images-fuchsia", 9, "zbi"),
            ),
            None,
        )
        .fuchsia_package(
            ::update_package::ImageMetadata::new(
                0,
                Hash::from_str(EMPTY_HASH).unwrap(),
                image_package_resource_url("update-images-fuchsia", 9, "zbi"),
            ),
            None,
        )
        .clone()
        .build();

    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder.insert_hook(mphooks::read_asset(|configuration, asset| {
                match (configuration, asset) {
                    (paver::Configuration::Recovery, paver::Asset::Kernel) => {
                        Ok(b"matching".to_vec())
                    }
                    (_, _) => Ok(vec![]),
                }
            }))
        })
        .build()
        .await;

    env.resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", serde_json::to_string(&images_json).unwrap());

    env.run_update_with_options("fuchsia-pkg://fuchsia.com/another-update/4", default_options())
        .await
        .expect("run system updater");

    let events = vec![
        Paver(PaverEvent::QueryCurrentConfiguration),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::A,
            asset: paver::Asset::VerifiedBootMetadata,
        }),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::A,
            asset: paver::Asset::Kernel,
        }),
        Paver(PaverEvent::QueryCurrentConfiguration),
        Paver(PaverEvent::QueryConfigurationStatus { configuration: paver::Configuration::A }),
        Paver(PaverEvent::SetConfigurationUnbootable { configuration: paver::Configuration::B }),
        Paver(PaverEvent::BootManagerFlush),
        PackageResolve("fuchsia-pkg://fuchsia.com/another-update/4".to_string()),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::B,
            asset: paver::Asset::Kernel,
        }),
        // Events we really care about testing
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::Recovery,
            asset: paver::Asset::Kernel,
        }),
        // rest of the events.
        Paver(PaverEvent::DataSinkFlush),
        ReplaceRetainedPackages(vec![]),
        Gc,
        BlobfsSync,
        Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
        Paver(PaverEvent::BootManagerFlush),
        Reboot,
    ];

    assert_eq!(env.take_interactions(), events);
}

#[fasync::run_singlethreaded(test)]
async fn writes_recovery() {
    let images_json = ::update_package::ImagePackagesManifest::builder()
        .recovery_package(
            ::update_package::ImageMetadata::new(
                5,
                hash(2),
                image_package_resource_url("update-images-recovery", 9, "recovery"),
            ),
            None,
        )
        .fuchsia_package(
            ::update_package::ImageMetadata::new(
                0,
                Hash::from_str(EMPTY_HASH).unwrap(),
                image_package_resource_url("update-images-fuchsia", 9, "zbi"),
            ),
            None,
        )
        .clone()
        .build();

    let env = TestEnv::builder().build().await;

    env.resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", serde_json::to_string(&images_json).unwrap());

    env.resolver.url(image_package_url_to_string("update-images-recovery", 9)).resolve(
        &env.resolver.package("recovery", hashstr(8)).add_file("recovery", "recovery zbi"),
    );

    env.run_update_with_options("fuchsia-pkg://fuchsia.com/another-update/4", default_options())
        .await
        .expect("run system updater");

    let events = vec![
        Paver(PaverEvent::QueryCurrentConfiguration),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::A,
            asset: paver::Asset::VerifiedBootMetadata,
        }),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::A,
            asset: paver::Asset::Kernel,
        }),
        Paver(PaverEvent::QueryCurrentConfiguration),
        Paver(PaverEvent::QueryConfigurationStatus { configuration: paver::Configuration::A }),
        Paver(PaverEvent::SetConfigurationUnbootable { configuration: paver::Configuration::B }),
        Paver(PaverEvent::BootManagerFlush),
        PackageResolve("fuchsia-pkg://fuchsia.com/another-update/4".to_string()),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::B,
            asset: paver::Asset::Kernel,
        }),
        // Events we care about testing
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::Recovery,
            asset: paver::Asset::Kernel,
        }),
        ReplaceRetainedPackages(vec![hash(9).into()]),
        Gc,
        PackageResolve(image_package_url_to_string("update-images-recovery", 9)),
        Paver(PaverEvent::WriteAsset {
            configuration: paver::Configuration::Recovery,
            asset: paver::Asset::Kernel,
            payload: b"recovery zbi".to_vec(),
        }),
        // rest of the events
        Paver(PaverEvent::DataSinkFlush),
        ReplaceRetainedPackages(vec![]),
        Gc,
        BlobfsSync,
        Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
        Paver(PaverEvent::BootManagerFlush),
        Reboot,
    ];

    assert_eq!(env.take_interactions(), events);
}

#[fasync::run_singlethreaded(test)]
async fn writes_recovery_vbmeta() {
    let images_json = ::update_package::ImagePackagesManifest::builder()
        .recovery_package(
            ::update_package::ImageMetadata::new(
                5,
                hash(2),
                image_package_resource_url("update-images-recovery", 9, "recovery"),
            ),
            Some(::update_package::ImageMetadata::new(
                1,
                hash(1),
                image_package_resource_url("update-images-recovery", 9, "recovery_vbmeta"),
            )),
        )
        .fuchsia_package(
            ::update_package::ImageMetadata::new(
                0,
                Hash::from_str(EMPTY_HASH).unwrap(),
                image_package_resource_url("update-images-fuchsia", 3, "zbi"),
            ),
            None,
        )
        .clone()
        .build();

    let env = TestEnv::builder().build().await;

    env.resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", serde_json::to_string(&images_json).unwrap());

    env.resolver.url(image_package_url_to_string("update-images-recovery", 9)).resolve(
        &env.resolver
            .package("recovery", hashstr(8))
            .add_file("recovery", "recovery zbi")
            .add_file("recovery_vbmeta", "rvbmeta"),
    );

    env.run_update_with_options("fuchsia-pkg://fuchsia.com/another-update/4", default_options())
        .await
        .expect("run system updater");

    let events = vec![
        Paver(PaverEvent::QueryCurrentConfiguration),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::A,
            asset: paver::Asset::VerifiedBootMetadata,
        }),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::A,
            asset: paver::Asset::Kernel,
        }),
        Paver(PaverEvent::QueryCurrentConfiguration),
        Paver(PaverEvent::QueryConfigurationStatus { configuration: paver::Configuration::A }),
        Paver(PaverEvent::SetConfigurationUnbootable { configuration: paver::Configuration::B }),
        Paver(PaverEvent::BootManagerFlush),
        PackageResolve("fuchsia-pkg://fuchsia.com/another-update/4".to_string()),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::B,
            asset: paver::Asset::Kernel,
        }),
        // Events we care about testing
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::Recovery,
            asset: paver::Asset::Kernel,
        }),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::Recovery,
            asset: paver::Asset::VerifiedBootMetadata,
        }),
        ReplaceRetainedPackages(vec![hash(9).into()]),
        Gc,
        PackageResolve(image_package_url_to_string("update-images-recovery", 9)),
        Paver(PaverEvent::WriteAsset {
            configuration: paver::Configuration::Recovery,
            asset: paver::Asset::Kernel,
            payload: b"recovery zbi".to_vec(),
        }),
        Paver(PaverEvent::WriteAsset {
            configuration: paver::Configuration::Recovery,
            asset: paver::Asset::VerifiedBootMetadata,
            payload: b"rvbmeta".to_vec(),
        }),
        // rest of the events
        Paver(PaverEvent::DataSinkFlush),
        ReplaceRetainedPackages(vec![]),
        Gc,
        BlobfsSync,
        Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
        Paver(PaverEvent::BootManagerFlush),
        Reboot,
    ];

    assert_eq!(env.take_interactions(), events);
}

#[fasync::run_singlethreaded(test)]
async fn recovery_present_but_should_write_recovery_is_false() {
    let images_json = ::update_package::ImagePackagesManifest::builder()
        .recovery_package(
            ::update_package::ImageMetadata::new(
                5,
                hash(2),
                image_package_resource_url("update-images-recovery", 3, "zbi"),
            ),
            None,
        )
        .fuchsia_package(
            ::update_package::ImageMetadata::new(
                5,
                hash(1),
                image_package_resource_url("update-images-fuchsia", 9, "zbi"),
            ),
            None,
        )
        .clone()
        .build();

    let env = TestEnv::builder().build().await;

    env.resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", serde_json::to_string(&images_json).unwrap());

    env.resolver
        .url(image_package_url_to_string("update-images-fuchsia", 9))
        .resolve(&env.resolver.package("fuchsia", hashstr(8)).add_file("zbi", "zbi contents"));

    env.run_update_with_options(
        "fuchsia-pkg://fuchsia.com/another-update/4",
        Options {
            initiator: Initiator::User,
            allow_attach_to_existing_attempt: true,
            should_write_recovery: false,
        },
    )
    .await
    .expect("run system updater");

    let events = vec![
        Paver(PaverEvent::QueryCurrentConfiguration),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::A,
            asset: paver::Asset::VerifiedBootMetadata,
        }),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::A,
            asset: paver::Asset::Kernel,
        }),
        Paver(PaverEvent::QueryCurrentConfiguration),
        Paver(PaverEvent::QueryConfigurationStatus { configuration: paver::Configuration::A }),
        Paver(PaverEvent::SetConfigurationUnbootable { configuration: paver::Configuration::B }),
        Paver(PaverEvent::BootManagerFlush),
        PackageResolve("fuchsia-pkg://fuchsia.com/another-update/4".to_string()),
        // Note that we never look at recovery because the flag indicated it should be skipped!
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::B,
            asset: paver::Asset::Kernel,
        }),
        ReplaceRetainedPackages(vec![hash(9).into()]),
        Gc,
        PackageResolve(image_package_url_to_string("update-images-fuchsia", 9)),
        Paver(PaverEvent::WriteAsset {
            configuration: paver::Configuration::B,
            asset: paver::Asset::Kernel,
            payload: b"zbi contents".to_vec(),
        }),
        Paver(PaverEvent::DataSinkFlush),
        ReplaceRetainedPackages(vec![]),
        Gc,
        BlobfsSync,
        Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
        Paver(PaverEvent::BootManagerFlush),
        Reboot,
    ];

    assert_eq!(env.take_interactions(), events);
}
