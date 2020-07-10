// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{merkle_str, *},
    pretty_assertions::assert_eq,
    serde_json::json,
};

#[fasync::run_singlethreaded(test)]
async fn succeeds_without_writable_data() {
    let env = TestEnv::new();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("zbi", "fake zbi");

    env.run_system_updater_args(
        SystemUpdaterArgs {
            oneshot: Some(true),
            initiator: Some(Initiator::User),
            target: Some("m3rk13"),
            ..Default::default()
        },
        SystemUpdaterEnv { mount_data: false, ..Default::default() },
    )
    .await
    .expect("run system updater");

    assert_eq!(env.read_history(), None);

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
async fn writes_history() {
    let env = TestEnv::new();

    let source = merkle_str!("ab");
    let target = merkle_str!("ba");

    assert_eq!(env.read_history(), None);

    env.resolver
        .register_package("update", UPDATE_HASH)
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("zbi", "fake zbi");

    env.run_system_updater(SystemUpdaterArgs {
        oneshot: Some(true),
        initiator: Some(Initiator::User),
        source: Some(source),
        target: Some(target),
        start: Some(1234567890),
        ..Default::default()
    })
    .await
    .unwrap();

    assert_eq!(
        env.read_history(),
        Some(json!({
            "source": source,
            "target": target,
            "start": 1234567890,
            "attempts": 1,
        }))
    );
}

#[fasync::run_singlethreaded(test)]
async fn replaces_bogus_history() {
    let env = TestEnv::new();

    env.write_history(json!({
        "valid": "no",
    }));

    env.resolver
        .register_package("update", UPDATE_HASH)
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("zbi", "fake zbi");

    env.run_system_updater(SystemUpdaterArgs {
        oneshot: Some(true),
        start: Some(42),
        ..Default::default()
    })
    .await
    .unwrap();

    assert_eq!(
        env.read_history(),
        Some(json!({
            "source": "",
            "target": "",
            "start": 42,
            "attempts": 1,
        }))
    );
}

#[fasync::run_singlethreaded(test)]
async fn increments_attempts_counter_on_retry() {
    let env = TestEnv::new();

    let source = merkle_str!("ab");
    let target = merkle_str!("ba");

    env.resolver.url("fuchsia-pkg://fuchsia.com/not-found").fail(Status::NOT_FOUND);
    env.resolver
        .register_package("update", UPDATE_HASH)
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("zbi", "fake zbi");

    let _ = env
        .run_system_updater(SystemUpdaterArgs {
            oneshot: Some(true),
            update: Some("fuchsia-pkg://fuchsia.com/not-found"),
            source: Some(source),
            target: Some(target),
            start: Some(10),
            ..Default::default()
        })
        .await
        .unwrap_err();

    env.run_system_updater(SystemUpdaterArgs {
        oneshot: Some(true),
        source: Some(source),
        target: Some(target),
        start: Some(20),
        ..Default::default()
    })
    .await
    .unwrap();

    assert_eq!(
        env.read_history(),
        Some(json!({
            "source": source,
            "target": target,
            "start": 20,
            "attempts": 2,
        }))
    );
}
