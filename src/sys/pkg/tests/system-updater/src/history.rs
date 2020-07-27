// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{merkle_str, *},
    fidl_fuchsia_pkg::PackageUrl,
    fidl_fuchsia_update_installer::{
        CompleteData, FetchData, InstallerMarker, Options, State, UpdateResult,
    },
    pretty_assertions::assert_eq,
    serde_json::json,
};

#[fasync::run_singlethreaded(test)]
async fn succeeds_without_writable_data() {
    let env = TestEnv::builder().oneshot(true).build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi");

    env.run_system_updater_oneshot_args(
        SystemUpdaterArgs {
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

/// Given a parsed update history value, verify each attempt contains an 'id', that no 2 'id'
/// fields are the same, and return the object with those fields removed.
fn strip_attempt_ids(mut value: serde_json::Value) -> serde_json::Value {
    let _ids = value
        .as_object_mut()
        .expect("top level is object")
        .get_mut("content")
        .expect("top level 'content' key")
        .as_array_mut()
        .expect("'content' is array")
        .iter_mut()
        .map(|attempt| attempt.as_object_mut().expect("attempt is object"))
        .fold(std::collections::HashSet::new(), |mut ids, attempt| {
            let attempt_id = match attempt.remove("id").expect("'id' field to be present") {
                serde_json::Value::String(id) => id,
                _ => panic!("expect attempt id to be a str"),
            };
            assert_eq!(ids.replace(attempt_id), None);
            ids
        });
    value
}

#[fasync::run_singlethreaded(test)]
async fn writes_history() {
    let env = TestEnv::builder().oneshot(true).build();

    // source/target CLI params are no longer trusted for update history values.
    let source = merkle_str!("ab");
    let target = merkle_str!("ba");

    assert_eq!(env.read_history(), None);

    env.resolver
        .register_package("update", UPDATE_HASH)
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi");

    env.run_system_updater_oneshot(SystemUpdaterArgs {
        initiator: Some(Initiator::User),
        source: Some(source),
        target: Some(target),
        start: Some(1234567890),
        ..Default::default()
    })
    .await
    .unwrap();

    assert_eq!(
        env.read_history().map(strip_attempt_ids),
        Some(json!({
            "version": "1",
            "content": [{
                "source": {
                    "update_hash": "",
                },
                "target": {
                    "update_hash": UPDATE_HASH,
                },
                "options": null,
                "url": "fuchsia-pkg://fuchsia.com/update",
                "start": 1234567890,
                "state": "COMPLETE",
            }],
        }))
    );
}

#[fasync::run_singlethreaded(test)]
async fn replaces_bogus_history() {
    let env = TestEnv::builder().oneshot(true).build();

    env.write_history(json!({
        "valid": "no",
    }));

    env.resolver
        .register_package("update", UPDATE_HASH)
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi");

    env.run_system_updater_oneshot(SystemUpdaterArgs { start: Some(42), ..Default::default() })
        .await
        .unwrap();

    assert_eq!(
        env.read_history().map(strip_attempt_ids),
        Some(json!({
            "version": "1",
            "content": [{
                "source": {
                    "update_hash": "",
                },
                "target": {
                    "update_hash": UPDATE_HASH,
                },
                "options": null,
                "url": "fuchsia-pkg://fuchsia.com/update",
                "start": 42,
                "state": "COMPLETE",
            }],
        }))
    );
}

#[fasync::run_singlethreaded(test)]
async fn increments_attempts_counter_on_retry() {
    let env = TestEnv::builder().oneshot(true).build();

    let source = merkle_str!("ab");
    let target = merkle_str!("ba");

    env.resolver.url("fuchsia-pkg://fuchsia.com/not-found").fail(Status::NOT_FOUND);
    env.resolver
        .register_package("update", UPDATE_HASH)
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi");

    let _ = env
        .run_system_updater_oneshot(SystemUpdaterArgs {
            update: Some("fuchsia-pkg://fuchsia.com/not-found"),
            source: Some(source),
            target: Some(target),
            start: Some(10),
            ..Default::default()
        })
        .await
        .unwrap_err();

    env.run_system_updater_oneshot(SystemUpdaterArgs {
        source: Some(source),
        target: Some(target),
        start: Some(20),
        ..Default::default()
    })
    .await
    .unwrap();

    assert_eq!(
        env.read_history().map(strip_attempt_ids),
        Some(json!({
            "version": "1",
            "content": [
            {
                "source": {
                    "update_hash": "",
                },
                "target": {
                    "update_hash": UPDATE_HASH,
                },
                "options": null,
                "url": "fuchsia-pkg://fuchsia.com/update",
                "start": 20,
                "state": "COMPLETE",
            },
            {
                "source": {
                    "update_hash": "",
                },
                "target": {
                    "update_hash": "",
                },
                "options": null,
                "url": "fuchsia-pkg://fuchsia.com/not-found",
                "start": 10,
                "state": "COMPLETE",
            }
            ],
        }))
    );
}

/// When there's history, the history FIDL APIs should return that history.
#[fasync::run_singlethreaded(test)]
async fn serves_fidl_with_history_present() {
    let env = TestEnv::builder()
        .history(json!({
            "version": "1",
            "content": [
            {
                "id": "1",
                "source": {
                    "update_hash": "",
                },
                "target": {
                    "update_hash": "",
                },
                "options": null,
                "url": "fuchsia-pkg://fuchsia.com/second-attempt",
                "start": 20,
                "state": "COMPLETE",
            },
            {
                "id": "0",
                "source": {
                    "update_hash": "",
                },
                "target": {
                    "update_hash": "",
                },
                "options": null,
                "url": "fuchsia-pkg://fuchsia.com/first-attempt",
                "start": 10,
                "state": "DOWNLOAD",
            }
            ],
        }))
        .build();

    let installer_proxy =
        env.system_updater.as_ref().unwrap().connect_to_service::<InstallerMarker>().unwrap();

    assert_eq!(
        installer_proxy.get_last_update_result().await.unwrap(),
        UpdateResult {
            attempt_id: Some("1".to_string()),
            url: Some(PackageUrl { url: "fuchsia-pkg://fuchsia.com/second-attempt".to_string() }),
            options: Some(Options {
                initiator: None,
                allow_attach_to_existing_attempt: None,
                should_write_recovery: None,
            }),
            state: Some(State::Complete(CompleteData { info: None, progress: None })),
        }
    );
    assert_eq!(
        installer_proxy.get_update_result("0").await.unwrap(),
        UpdateResult {
            attempt_id: Some("0".to_string()),
            url: Some(PackageUrl { url: "fuchsia-pkg://fuchsia.com/first-attempt".to_string() }),
            options: Some(Options {
                initiator: None,
                allow_attach_to_existing_attempt: None,
                should_write_recovery: None,
            }),
            state: Some(State::Fetch(FetchData { info: None, progress: None })),
        }
    );
    assert_eq!(
        installer_proxy.get_update_result("1").await.unwrap(),
        UpdateResult {
            attempt_id: Some("1".to_string()),
            url: Some(PackageUrl { url: "fuchsia-pkg://fuchsia.com/second-attempt".to_string() }),
            options: Some(Options {
                initiator: None,
                allow_attach_to_existing_attempt: None,
                should_write_recovery: None,
            }),
            state: Some(State::Complete(CompleteData { info: None, progress: None })),
        }
    );
}

/// When there's no history, the history FIDL APIs should return results with empty fields.
#[fasync::run_singlethreaded(test)]
async fn serves_fidl_without_history_present() {
    let env = TestEnv::new();

    let installer_proxy =
        env.system_updater.as_ref().unwrap().connect_to_service::<InstallerMarker>().unwrap();

    assert_eq!(
        installer_proxy.get_last_update_result().await.unwrap(),
        UpdateResult { attempt_id: None, url: None, options: None, state: None }
    );
    assert_eq!(
        installer_proxy.get_update_result("0").await.unwrap(),
        UpdateResult { attempt_id: None, url: None, options: None, state: None }
    );
}
