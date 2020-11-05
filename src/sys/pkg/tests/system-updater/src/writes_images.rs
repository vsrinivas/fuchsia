// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::*, pretty_assertions::assert_eq};

#[fasync::run_singlethreaded(test)]
async fn fails_on_paver_connect_error() {
    let env = TestEnv::builder().unregister_protocol(Protocol::Paver).build();

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
async fn fails_on_image_write_error() {
    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder.call_hook(|event| match event {
                PaverEvent::WriteAsset { .. } => Status::INTERNAL,
                _ => Status::OK,
            })
        })
        .build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake_zbi");

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator: metrics::OtaResultAttemptsMetricDimensionInitiator::UserInitiatedCheck
                as u32,
            phase: metrics::OtaResultAttemptsMetricDimensionPhase::ImageWrite as u32,
            status_code: metrics::OtaResultAttemptsMetricDimensionStatusCode::Error as u32,
            target: "".into(),
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
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            BlobfsSync,
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::B,
                asset: paver::Asset::Kernel,
                payload: b"fake_zbi".to_vec(),
            },),
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn skip_recovery_does_not_write_recovery_or_vbmeta() {
    let env = TestEnv::builder().build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi")
        .add_file("zedboot", "new recovery")
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
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
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
async fn writes_to_both_configs_if_abr_not_supported() {
    let env = TestEnv::builder()
        .paver_service(|builder| builder.boot_manager_close_with_epitaph(Status::NOT_SUPPORTED))
        .build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake_zbi");

    env.run_update().await.expect("success");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator: metrics::OtaResultAttemptsMetricDimensionInitiator::UserInitiatedCheck
                as u32,
            phase: metrics::OtaResultAttemptsMetricDimensionPhase::SuccessPendingReboot as u32,
            status_code: metrics::OtaResultAttemptsMetricDimensionStatusCode::Success as u32,
            target: "".into(),
        }
    );

    assert_eq!(
        env.take_interactions(),
        vec![
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            BlobfsSync,
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
            Reboot,
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
                .config_status_hook(|_| paver::ConfigurationStatus::Pending)
                .current_config(current_config)
        })
        .build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake_zbi");

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator: metrics::OtaResultAttemptsMetricDimensionInitiator::UserInitiatedCheck
                as u32,
            phase: metrics::OtaResultAttemptsMetricDimensionPhase::Tufupdate as u32,
            status_code: metrics::OtaResultAttemptsMetricDimensionStatusCode::Error as u32,
            target: "".into(),
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
async fn does_not_update_if_alternate_cant_be_marked_unbootable() {
    let current_config = paver::Configuration::A;

    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder
                .call_hook(|event| match event {
                    PaverEvent::SetConfigurationUnbootable { .. } => Status::INTERNAL,
                    _ => Status::OK,
                })
                .current_config(current_config)
        })
        .build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake_zbi");

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator: metrics::OtaResultAttemptsMetricDimensionInitiator::UserInitiatedCheck
                as u32,
            phase: metrics::OtaResultAttemptsMetricDimensionPhase::Tufupdate as u32,
            status_code: metrics::OtaResultAttemptsMetricDimensionStatusCode::Error as u32,
            target: "".into(),
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
async fn writes_recovery_called_legacy_zedboot() {
    let env = TestEnv::builder().build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi")
        .add_file("zedboot", "new recovery");

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
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            BlobfsSync,
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
            Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
            Paver(PaverEvent::DataSinkFlush),
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}

// TODO(fxbug.dev/52356): drop this duplicate test when "zedboot" is no longer allowed/used.
#[fasync::run_singlethreaded(test)]
async fn writes_recovery() {
    let env = TestEnv::builder().build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
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
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            BlobfsSync,
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
            Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
            Paver(PaverEvent::DataSinkFlush),
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn writes_recovery_vbmeta() {
    let env = TestEnv::builder().build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi")
        .add_file("zedboot", "new recovery")
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
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            BlobfsSync,
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
            Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
            Paver(PaverEvent::DataSinkFlush),
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn writes_fuchsia_vbmeta() {
    let env = TestEnv::builder().build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
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
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            BlobfsSync,
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
            Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
            Paver(PaverEvent::DataSinkFlush),
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}

// Run an update with a given current config, assert that it succeeded, and return its interactions.
async fn update_with_current_config(
    current_config: paver::Configuration,
) -> Vec<SystemUpdaterInteraction> {
    let env =
        TestEnv::builder().paver_service(|builder| builder.current_config(current_config)).build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake_zbi");

    env.run_update().await.expect("success");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator: metrics::OtaResultAttemptsMetricDimensionInitiator::UserInitiatedCheck
                as u32,
            phase: metrics::OtaResultAttemptsMetricDimensionPhase::SuccessPendingReboot as u32,
            status_code: metrics::OtaResultAttemptsMetricDimensionStatusCode::Success as u32,
            target: "".into(),
        }
    );

    env.take_interactions()
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
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            BlobfsSync,
            Paver(PaverEvent::WriteAsset {
                configuration: target_config,
                asset: paver::Asset::Kernel,
                payload: b"fake_zbi".to_vec(),
            }),
            Paver(PaverEvent::SetConfigurationActive { configuration: target_config }),
            Paver(PaverEvent::DataSinkFlush),
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}
