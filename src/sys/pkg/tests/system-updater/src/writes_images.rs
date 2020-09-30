// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::*, pretty_assertions::assert_eq};

#[fasync::run_singlethreaded(test)]
async fn fails_on_paver_connect_error() {
    let env = TestEnv::builder().unregister_protocol(Protocol::Paver).oneshot(true).build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake_zbi");

    let result = env
        .run_system_updater_oneshot(SystemUpdaterArgs {
            initiator: Some(Initiator::User),
            target: Some("m3rk13"),
            ..Default::default()
        })
        .await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    // Appmgr will close the paver service channel when it is unable to forward the channel to any
    // implementation of that protocol, but it is a race condition as to whether or not the system
    // updater will be able to send the requests to open the data sink and boot manager connections
    // before that happens. So, the update attempt will either fail very early or when it attempts
    // to query the active configuration.
    let interactions = env.take_interactions();
    assert!(
        interactions == &[]
            || interactions == &[Gc, PackageResolve(UPDATE_PKG_URL.to_string()), Gc, BlobfsSync,],
        "expected early failure or failure while querying active configuration. Got {:#?}",
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
        .oneshot(true)
        .build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake_zbi");

    let result = env
        .run_system_updater_oneshot(SystemUpdaterArgs {
            initiator: Some(Initiator::User),
            target: Some("m3rk13"),
            ..Default::default()
        })
        .await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    let loggers = env.logger_factory.loggers.lock().clone();
    assert_eq!(loggers.len(), 1);
    let logger = loggers.into_iter().next().unwrap();
    assert_eq!(
        OtaMetrics::from_events(logger.cobalt_events.lock().clone()),
        OtaMetrics {
            initiator: metrics::OtaResultAttemptsMetricDimensionInitiator::UserInitiatedCheck
                as u32,
            phase: metrics::OtaResultAttemptsMetricDimensionPhase::ImageWrite as u32,
            status_code: metrics::OtaResultAttemptsMetricDimensionStatusCode::Error as u32,
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
            },),
            Paver(PaverEvent::ReadAsset {
                configuration: paver::Configuration::A,
                asset: paver::Asset::Kernel
            },),
            Paver(PaverEvent::QueryCurrentConfiguration,),
            Paver(PaverEvent::QueryActiveConfiguration,),
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
    let env = TestEnv::builder().oneshot(true).build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi")
        .add_file("zedboot", "new recovery")
        .add_file("recovery.vbmeta", "new recovery vbmeta");

    env.run_system_updater_oneshot(SystemUpdaterArgs {
        initiator: Some(Initiator::User),
        target: Some("m3rk13"),
        skip_recovery: Some(true),
        ..Default::default()
    })
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
            Paver(PaverEvent::QueryActiveConfiguration),
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
        .oneshot(true)
        .build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake_zbi");

    env.run_system_updater_oneshot(SystemUpdaterArgs {
        initiator: Some(Initiator::User),
        target: Some("m3rk13"),
        ..Default::default()
    })
    .await
    .expect("success");

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
// If we can't ensure that the current partition == the active partition and the active partition is
// healthy, system-updater makes progress
async fn updates_even_if_cant_set_active_partition_healthy() {
    let current_config = paver::Configuration::A;
    let active_config = paver::Configuration::B;

    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder
                .call_hook(|event| match event {
                    PaverEvent::SetActiveConfigurationHealthy => Status::INTERNAL,
                    _ => Status::OK,
                })
                .current_config(current_config)
                .active_config(active_config)
        })
        .oneshot(true)
        .build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake_zbi");

    env.run_system_updater_oneshot(SystemUpdaterArgs {
        initiator: Some(Initiator::User),
        target: Some("m3rk13"),
        ..Default::default()
    })
    .await
    .expect("success");

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
                configuration: current_config,
                asset: paver::Asset::VerifiedBootMetadata
            }),
            Paver(PaverEvent::ReadAsset {
                configuration: current_config,
                asset: paver::Asset::Kernel
            }),
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::QueryActiveConfiguration),
            Paver(PaverEvent::QueryConfigurationStatus { configuration: current_config }),
            Paver(PaverEvent::SetConfigurationActive { configuration: current_config }),
            Paver(PaverEvent::SetActiveConfigurationHealthy),
            Paver(PaverEvent::BootManagerFlush),
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            BlobfsSync,
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::B,
                asset: paver::Asset::Kernel,
                payload: b"fake_zbi".to_vec(),
            }),
            Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
            Paver(PaverEvent::DataSinkFlush),
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}

// Assert that when current partition == active partition, we write to the non-current partition.
#[fasync::run_singlethreaded(test)]
async fn writes_to_non_current_config_if_abr_supported_and_current_config_a() {
    assert_writes_for_active_equal_to_current(
        paver::Configuration::A,
        paver::Configuration::A,
        paver::Configuration::B,
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn writes_to_non_current_config_if_abr_supported_and_current_config_b() {
    assert_writes_for_active_equal_to_current(
        paver::Configuration::B,
        paver::Configuration::B,
        paver::Configuration::A,
    )
    .await
}

// When current partition != active partition, and current is healthy,
// we should reset active to current, and set the active configuration healthy.
#[fasync::run_singlethreaded(test)]
async fn resets_active_if_active_not_equal_to_current_with_current_a() {
    assert_resets_active_when_active_not_equal_to_current(
        paver::Configuration::A,
        paver::Configuration::B,
        paver::Configuration::B,
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn resets_active_if_active_not_equal_to_current_with_current_b() {
    assert_resets_active_when_active_not_equal_to_current(
        paver::Configuration::B,
        paver::Configuration::A,
        paver::Configuration::A,
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn does_not_reset_active_if_active_not_equal_to_current_with_current_r() {
    // Since recovery cannot be the active partition,
    // we won't run the logic which resets current to active. This should take the "normal"
    // path and not attempt to reset the active partition,
    // then write to A, which is the default if we are in recovery.
    let current_config = paver::Configuration::Recovery;
    let active_config = paver::Configuration::A;
    let target_config = paver::Configuration::A;
    assert_eq!(
        update_with_current_and_active_configurations(current_config, active_config).await,
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

// If current is not equal to active and current is not healthy
// we'll reset active to current, but also NOT set the current partition status to healthy.
#[fasync::run_singlethreaded(test)]
async fn resets_active_with_unhealthy_current_a() {
    assert_resets_active_with_unhealthy_current_partition(
        paver::Configuration::A,
        paver::Configuration::B,
        paver::Configuration::B,
    )
    .await
}

// Test that if current is not equal to active and current is not healthy
// we'll reset active to current, but also NOT set the current partition status to healthy.
#[fasync::run_singlethreaded(test)]
async fn resets_active_with_unhealthy_current_b() {
    assert_resets_active_with_unhealthy_current_partition(
        paver::Configuration::B,
        paver::Configuration::A,
        paver::Configuration::A,
    )
    .await
}

// Note that we don't test resetting active with current == Recovery,
// because we'll only run the resetting logic if current is either A or B (that flow
// is tested in resets_active_if_active_not_equal_to_current_with_current_r)

#[fasync::run_singlethreaded(test)]
async fn writes_recovery_called_legacy_zedboot() {
    let env = TestEnv::builder().oneshot(true).build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi")
        .add_file("zedboot", "new recovery");

    env.run_system_updater_oneshot(SystemUpdaterArgs {
        initiator: Some(Initiator::User),
        target: Some("m3rk13"),
        ..Default::default()
    })
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
            Paver(PaverEvent::QueryActiveConfiguration),
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
    let env = TestEnv::builder().oneshot(true).build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi")
        .add_file("recovery", "new recovery");

    env.run_system_updater_oneshot(SystemUpdaterArgs {
        initiator: Some(Initiator::User),
        target: Some("m3rk13"),
        ..Default::default()
    })
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
            Paver(PaverEvent::QueryActiveConfiguration),
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
    let env = TestEnv::builder().oneshot(true).build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi")
        .add_file("zedboot", "new recovery")
        .add_file("recovery.vbmeta", "new recovery vbmeta");

    env.run_system_updater_oneshot(SystemUpdaterArgs {
        initiator: Some(Initiator::User),
        target: Some("m3rk13"),
        ..Default::default()
    })
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
            Paver(PaverEvent::QueryActiveConfiguration),
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
    let env = TestEnv::builder().oneshot(true).build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi")
        .add_file("fuchsia.vbmeta", "fake zbi vbmeta");

    env.run_system_updater_oneshot(SystemUpdaterArgs {
        initiator: Some(Initiator::User),
        target: Some("m3rk13"),
        ..Default::default()
    })
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
            Paver(PaverEvent::QueryActiveConfiguration),
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

// Run an update with a given current and active config, and a customized config_status hook for the boot manager.
// Useful for setting a given configuration to Unbootable or Pending.
async fn update_with_custom_config_status(
    current_config: paver::Configuration,
    active_config: paver::Configuration,
    config_status_hook: Option<
        Box<dyn Fn(&PaverEvent) -> fidl_fuchsia_paver::ConfigurationStatus + Send + Sync>,
    >,
) -> Vec<SystemUpdaterInteraction> {
    let env = TestEnv::builder()
        .paver_service(|builder| {
            let builder = builder.current_config(current_config).active_config(active_config);

            if let Some(hook) = config_status_hook {
                builder.config_status_hook(hook)
            } else {
                builder
            }
        })
        .oneshot(true)
        .build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake_zbi");

    env.run_system_updater_oneshot(SystemUpdaterArgs {
        initiator: Some(Initiator::User),
        target: Some("m3rk13"),
        ..Default::default()
    })
    .await
    .expect("success");

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

    env.take_interactions()
}

// Run an update with a given current and active config, but an unchanged boot_manager config status configuration.
async fn update_with_current_and_active_configurations(
    current_config: paver::Configuration,
    active_config: paver::Configuration,
) -> Vec<SystemUpdaterInteraction> {
    update_with_custom_config_status(current_config, active_config, None).await
}

// When the current partition is also the active partition, we should see a "normal" update flow.
async fn assert_writes_for_active_equal_to_current(
    current_config: paver::Configuration,
    active_config: paver::Configuration,
    target_config: paver::Configuration,
) {
    assert_eq!(
        update_with_current_and_active_configurations(current_config, active_config).await,
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
            Paver(PaverEvent::QueryActiveConfiguration),
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

// When the current partition is not the active partition and current is healthy,
// system-updater should reset active to current, and then set active to healthy.
async fn assert_resets_active_when_active_not_equal_to_current(
    current_config: paver::Configuration,
    active_config: paver::Configuration,
    target_config: paver::Configuration,
) {
    let interactions =
        update_with_current_and_active_configurations(current_config, active_config).await;

    assert_eq!(
        interactions,
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
            // Get the current and active partitions, check if they match.
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::QueryActiveConfiguration),
            // They don't match, so get the status of current, set current to active.
            Paver(PaverEvent::QueryConfigurationStatus { configuration: current_config }),
            Paver(PaverEvent::SetConfigurationActive { configuration: current_config }),
            // Depending on the original health status of the current partition, set active healthy
            Paver(PaverEvent::SetActiveConfigurationHealthy),
            // Set the old active unbootable and flush
            Paver(PaverEvent::SetConfigurationUnbootable { configuration: active_config }),
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

// When current is not equal to active but current is not healthy, system-updater should not set active to healthy.
async fn assert_resets_active_with_unhealthy_current_partition(
    current_config: paver::Configuration,
    active_config: paver::Configuration,
    target_config: paver::Configuration,
) {
    // Set the health of the current partition.
    let current_config_clone = current_config.clone();
    let interactions = update_with_custom_config_status(
        current_config,
        active_config,
        Some(Box::new(move |event| {
            if let PaverEvent::QueryConfigurationStatus { configuration } = event {
                if *configuration == current_config_clone {
                    // The current config is unbootable, all others should be fine.
                    return fidl_fuchsia_paver::ConfigurationStatus::Unbootable;
                }
            }
            fidl_fuchsia_paver::ConfigurationStatus::Healthy
        })),
    )
    .await;

    assert_eq!(
        interactions,
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
            // Get the current and active partitions, check if they match.
            Paver(PaverEvent::QueryCurrentConfiguration),
            Paver(PaverEvent::QueryActiveConfiguration),
            // They don't match, so get the status of current, set current to active.
            Paver(PaverEvent::QueryConfigurationStatus { configuration: current_config }),
            Paver(PaverEvent::SetConfigurationActive { configuration: current_config }),
            // The current partition was not originally healthy, so we shouldn't set active healthy now.
            // If it was healthy, we'd expect to see `Paver(PaverEvent::SetActiveConfigurationHealthy)`,

            // Set the old active unbootable and flush
            Paver(PaverEvent::SetConfigurationUnbootable { configuration: active_config }),
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
