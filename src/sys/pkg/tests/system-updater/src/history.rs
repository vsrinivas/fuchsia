// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    assert_matches::assert_matches,
    chrono::prelude::*,
    fidl_fuchsia_pkg::PackageUrl,
    fidl_fuchsia_update_installer::{
        CompleteData, FetchData, InstallationProgress, Options, State, UpdateInfo, UpdateResult,
    },
    pretty_assertions::assert_eq,
    serde_json::json,
};

#[fasync::run_singlethreaded(test)]
async fn succeeds_without_writable_data_v1() {
    let env = TestEnv::builder().mount_data(false).build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("zbi", "fake zbi");

    env.run_update().await.expect("run system updater");

    assert_eq!(env.read_history(), None);

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator: metrics::OtaResultAttemptsMetricDimensionInitiator::UserInitiatedCheck
                as u32,
            phase: metrics::OtaResultAttemptsMetricDimensionPhase::SuccessPendingReboot as u32,
            status_code: metrics::OtaResultAttemptsMetricDimensionStatusCode::Success as u32,
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
            Paver(PaverEvent::BootManagerFlush),
            Reboot,
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn succeeds_without_writable_data() {
    let env = TestEnv::builder().mount_data(false).build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", make_images_json_zbi());

    env.run_update().await.expect("run system updater");

    assert_eq!(env.read_history(), None);

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator: metrics::OtaResultAttemptsMetricDimensionInitiator::UserInitiatedCheck
                as u32,
            phase: metrics::OtaResultAttemptsMetricDimensionPhase::SuccessPendingReboot as u32,
            status_code: metrics::OtaResultAttemptsMetricDimensionStatusCode::Success as u32,
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

/// Given a parsed update history value, verify each attempt contains a 'start' time that's later
/// than 01/01/2020, and return the object with those fields removed.
fn strip_start_time(mut value: serde_json::Value) -> serde_json::Value {
    let min_start_time = Utc.ymd(2020, 1, 1).and_hms(0, 0, 0).timestamp_nanos() as u64;
    value
        .as_object_mut()
        .expect("top level is object")
        .get_mut("content")
        .expect("top level 'content' key")
        .as_array_mut()
        .expect("'content' is array")
        .iter_mut()
        .map(|attempt| attempt.as_object_mut().expect("attempt is object"))
        .for_each(|attempt| {
            assert_matches!(
                attempt.remove("start"),
                Some(serde_json::Value::Number(start)) if start.as_u64().expect("start is u64") > min_start_time
            );
        });
    value
}

#[fasync::run_singlethreaded(test)]
async fn writes_history_v1() {
    let env = TestEnv::builder()
        .system_image_hash(
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap(),
        )
        .build()
        .await;

    assert_eq!(env.read_history(), None);

    env.set_build_version("0.1");

    env.resolver
        .register_package("update", UPDATE_HASH)
        .add_file("packages.json", make_packages_json(["fuchsia-pkg://fuchsia.com/system_image/0?hash=838b5199d12c8ff4ef92bfd9771d2f8781b7b8fd739dd59bcf63f353a1a93f67"]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("zbi", "fake zbi")
        .add_file("fuchsia.vbmeta", "vbmeta")
        .add_file("version", "0.2");
    env.resolver.register_package(
        "system_image/0?hash=838b5199d12c8ff4ef92bfd9771d2f8781b7b8fd739dd59bcf63f353a1a93f67",
        "838b5199d12c8ff4ef92bfd9771d2f8781b7b8fd739dd59bcf63f353a1a93f67",
    );

    env.run_update_with_options(
        UPDATE_PKG_URL,
        fidl_fuchsia_update_installer_ext::Options {
            initiator: Initiator::Service,
            allow_attach_to_existing_attempt: false,
            should_write_recovery: true,
        },
    )
    .await
    .unwrap();

    assert_eq!(
        env.read_history().map(strip_attempt_ids).map(strip_start_time),
        Some(json!({
            "version": "1",
            "content": [{
                "source": {
                    "update_hash": "",
                    "system_image_hash":
                        "0000000000000000000000000000000000000000000000000000000000000000",
                    "vbmeta_hash":
                        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    "zbi_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    "build_version": "0.1.0.0",
                    "epoch": SOURCE_EPOCH.to_string()
                },
                "target": {
                    "update_hash": UPDATE_HASH,
                    "system_image_hash":
                        "838b5199d12c8ff4ef92bfd9771d2f8781b7b8fd739dd59bcf63f353a1a93f67",
                    "vbmeta_hash":
                        "a0c6f07a4b3a17fb9348db981de3c5602e2685d626599be1bd909195c694a57b",
                    "zbi_hash": "543b8066d52d734f69794fd0594ba78a5b8e11124d51f4d549dd6534d46da73e",
                    "build_version": "0.2.0.0",
                    "epoch": SOURCE_EPOCH.to_string()
                },
                "options": {
                    "allow_attach_to_existing_attempt": false,
                    "initiator": "Service",
                    "should_write_recovery": true,
                },
                "url": UPDATE_PKG_URL,
                "state": {
                    "id": "reboot",
                    "info": {
                        "download_size": 0,
                    },
                    "progress": {
                        "bytes_downloaded": 0,
                        "fraction_completed": 1.0,
                    },
                },
            }],
        }))
    );
}

#[fasync::run_singlethreaded(test)]
async fn writes_history() {
    let images_json = ::update_package::ImagePackagesManifest::builder()
        .fuchsia_package(
            ::update_package::ImageMetadata::new(
                8,
                hash(6),
                image_package_resource_url("update-images-fuchsia", 9, "zbi"),
            ),
            Some(::update_package::ImageMetadata::new(
                6,
                hash(3),
                image_package_resource_url("update-images-fuchsia", 9, "vbmeta"),
            )),
        )
        .clone()
        .build();

    let env = TestEnv::builder()
        .system_image_hash(
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap(),
        )
        .build()
        .await;

    assert_eq!(env.read_history(), None);

    env.set_build_version("0.1");

    env.resolver
        .register_package("update", UPDATE_HASH)
        .add_file("packages.json", make_packages_json(["fuchsia-pkg://fuchsia.com/system_image/0?hash=838b5199d12c8ff4ef92bfd9771d2f8781b7b8fd739dd59bcf63f353a1a93f67"]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", serde_json::to_string(&images_json).unwrap())
        .add_file("version", "0.2");
    env.resolver.register_package(
        "system_image/0?hash=838b5199d12c8ff4ef92bfd9771d2f8781b7b8fd739dd59bcf63f353a1a93f67",
        "838b5199d12c8ff4ef92bfd9771d2f8781b7b8fd739dd59bcf63f353a1a93f67",
    );

    env.resolver.url(image_package_url_to_string("update-images-fuchsia", 9)).resolve(
        &env.resolver
            .package("fuchsia", hashstr(8))
            .add_file("zbi", "some zbi")
            .add_file("vbmeta", "vbmeta contents"),
    );

    env.run_update_with_options(
        UPDATE_PKG_URL,
        fidl_fuchsia_update_installer_ext::Options {
            initiator: Initiator::Service,
            allow_attach_to_existing_attempt: false,
            should_write_recovery: true,
        },
    )
    .await
    .unwrap();

    assert_eq!(
        env.read_history().map(strip_attempt_ids).map(strip_start_time),
        Some(json!({
            "version": "1",
            "content": [{
                "source": {
                    "update_hash": "",
                    "system_image_hash":
                        "0000000000000000000000000000000000000000000000000000000000000000",
                    "vbmeta_hash":
                        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    "zbi_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    "build_version": "0.1.0.0",
                    "epoch": SOURCE_EPOCH.to_string()
                },
                "target": {
                    "update_hash": UPDATE_HASH,
                    "system_image_hash":
                        "838b5199d12c8ff4ef92bfd9771d2f8781b7b8fd739dd59bcf63f353a1a93f67",
                    "vbmeta_hash":
                        hashstr(3),
                    "zbi_hash": hashstr(6),
                    "build_version": "0.2.0.0",
                    "epoch": SOURCE_EPOCH.to_string()
                },
                "options": {
                    "allow_attach_to_existing_attempt": false,
                    "initiator": "Service",
                    "should_write_recovery": true,
                },
                "url": UPDATE_PKG_URL,
                "state": {
                    "id": "reboot",
                    "info": {
                        "download_size": 0,
                    },
                    "progress": {
                        "bytes_downloaded": 0,
                        "fraction_completed": 1.0,
                    },
                },
            }],
        }))
    );
}

#[fasync::run_singlethreaded(test)]
async fn replaces_bogus_history_v1() {
    let env = TestEnv::builder().build().await;

    env.write_history(json!({
        "valid": "no",
    }));

    env.resolver
        .register_package("update", UPDATE_HASH)
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("zbi", "fake zbi");

    env.run_update().await.unwrap();

    assert_eq!(
        env.read_history().map(strip_attempt_ids).map(strip_start_time),
        Some(json!({
            "version": "1",
            "content": [{
                "source": {
                    "update_hash": "",
                    "system_image_hash": "",
                    "vbmeta_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    "zbi_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    "build_version": "",
                    "epoch": SOURCE_EPOCH.to_string()
                },
                "target": {
                    "update_hash": UPDATE_HASH,
                    "system_image_hash": "",
                    "vbmeta_hash": "",
                    "zbi_hash": "543b8066d52d734f69794fd0594ba78a5b8e11124d51f4d549dd6534d46da73e",
                    "build_version": "",
                    "epoch": SOURCE_EPOCH.to_string()
                },
                "options": {
                    "allow_attach_to_existing_attempt": true,
                    "initiator": "User",
                    "should_write_recovery": true,
                },
                "url": UPDATE_PKG_URL,
                "state": {
                    "id": "reboot",
                    "info": {
                        "download_size": 0,
                    },
                    "progress": {
                        "bytes_downloaded": 0,
                        "fraction_completed": 1.0,
                    },
                },
            }],
        }))
    );
}

#[fasync::run_singlethreaded(test)]
async fn replaces_bogus_history() {
    let env = TestEnv::builder().build().await;

    env.write_history(json!({
        "valid": "no",
    }));

    env.resolver
        .register_package("update", UPDATE_HASH)
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", make_images_json_zbi());

    env.run_update().await.unwrap();

    assert_eq!(
        env.read_history().map(strip_attempt_ids).map(strip_start_time),
        Some(json!({
            "version": "1",
            "content": [{
                "source": {
                    "update_hash": "",
                    "system_image_hash": "",
                    "vbmeta_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    "zbi_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    "build_version": "",
                    "epoch": SOURCE_EPOCH.to_string()
                },
                "target": {
                    "update_hash": UPDATE_HASH,
                    "system_image_hash": "",
                    "vbmeta_hash": "",
                    "zbi_hash": EMPTY_HASH,
                    "build_version": "",
                    "epoch": SOURCE_EPOCH.to_string()
                },
                "options": {
                    "allow_attach_to_existing_attempt": true,
                    "initiator": "User",
                    "should_write_recovery": true,
                },
                "url": UPDATE_PKG_URL,
                "state": {
                    "id": "reboot",
                    "info": {
                        "download_size": 0,
                    },
                    "progress": {
                        "bytes_downloaded": 0,
                        "fraction_completed": 1.0,
                    },
                },
            }],
        }))
    );
}

#[fasync::run_singlethreaded(test)]
async fn increments_attempts_counter_on_retry_v1() {
    let env = TestEnv::builder().build().await;

    env.resolver
        .url("fuchsia-pkg://fuchsia.com/not-found")
        .fail(fidl_fuchsia_pkg::ResolveError::PackageNotFound);
    env.resolver
        .register_package("update", UPDATE_HASH)
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("zbi", "fake zbi");

    let _ = env
        .run_update_with_options(
            "fuchsia-pkg://fuchsia.com/not-found",
            fidl_fuchsia_update_installer_ext::Options {
                initiator: Initiator::Service,
                allow_attach_to_existing_attempt: false,
                should_write_recovery: true,
            },
        )
        .await
        .unwrap_err();

    env.run_update().await.unwrap();

    assert_eq!(
        env.read_history().map(strip_attempt_ids).map(strip_start_time),
        Some(json!({
            "version": "1",
            "content": [
            {
                "source": {
                    "update_hash": "",
                    "system_image_hash": "",
                    "vbmeta_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    "zbi_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    "build_version": "",
                    "epoch": SOURCE_EPOCH.to_string()
                },
                "target": {
                    "update_hash": UPDATE_HASH,
                    "system_image_hash": "",
                    "vbmeta_hash": "",
                    "zbi_hash": "543b8066d52d734f69794fd0594ba78a5b8e11124d51f4d549dd6534d46da73e",
                    "build_version": "",
                    "epoch": SOURCE_EPOCH.to_string()
                },
                "options": {
                    "allow_attach_to_existing_attempt": true,
                    "initiator": "User",
                    "should_write_recovery": true,
                },
                "url": "fuchsia-pkg://fuchsia.com/update",
                "state": {
                    "id": "reboot",
                    "info": {
                        "download_size": 0,
                    },
                    "progress": {
                        "bytes_downloaded": 0,
                        "fraction_completed": 1.0,
                    },
                },
            },
            {
                "source": {
                    "update_hash": "",
                    "system_image_hash": "",
                    "vbmeta_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    "zbi_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    "build_version": "",
                    "epoch": SOURCE_EPOCH.to_string()
                },
                "target": {
                    "update_hash": "",
                    "system_image_hash": "",
                    "vbmeta_hash": "",
                    "zbi_hash": "",
                    "build_version": "",
                    "epoch": "",
                },
                "options": {
                    "allow_attach_to_existing_attempt": false,
                    "initiator": "Service",
                    "should_write_recovery": true,
                },
                "url": "fuchsia-pkg://fuchsia.com/not-found",
                "state": {
                    "id": "fail_prepare",
                    "reason": "internal",
                },
            }
            ],
        }))
    );
}

#[fasync::run_singlethreaded(test)]
async fn increments_attempts_counter_on_retry() {
    let images_json = ::update_package::ImagePackagesManifest::builder()
        .fuchsia_package(
            ::update_package::ImageMetadata::new(
                8,
                hash(6),
                image_package_resource_url("update-images-fuchsia", 9, "zbi"),
            ),
            Some(::update_package::ImageMetadata::new(
                6,
                hash(3),
                image_package_resource_url("update-images-fuchsia", 9, "vbmeta"),
            )),
        )
        .clone()
        .build();

    let env = TestEnv::builder().build().await;

    env.resolver
        .url("fuchsia-pkg://fuchsia.com/not-found")
        .fail(fidl_fuchsia_pkg::ResolveError::PackageNotFound);
    env.resolver
        .register_package("update", UPDATE_HASH)
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", serde_json::to_string(&images_json).unwrap());

    env.resolver.url(image_package_url_to_string("update-images-fuchsia", 9)).resolve(
        &env.resolver
            .package("fuchsia", hashstr(8))
            .add_file("zbi", "some zbi")
            .add_file("vbmeta", "vbmeta contents"),
    );

    let _ = env
        .run_update_with_options(
            "fuchsia-pkg://fuchsia.com/not-found",
            fidl_fuchsia_update_installer_ext::Options {
                initiator: Initiator::Service,
                allow_attach_to_existing_attempt: false,
                should_write_recovery: true,
            },
        )
        .await
        .unwrap_err();

    env.run_update().await.unwrap();

    assert_eq!(
        env.read_history().map(strip_attempt_ids).map(strip_start_time),
        Some(json!({
            "version": "1",
            "content": [
            {
                "source": {
                    "update_hash": "",
                    "system_image_hash": "",
                    "vbmeta_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    "zbi_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    "build_version": "",
                    "epoch": SOURCE_EPOCH.to_string()
                },
                "target": {
                    "update_hash": UPDATE_HASH,
                    "system_image_hash": "",
                    "vbmeta_hash": hashstr(3),
                    "zbi_hash": hashstr(6),
                    "build_version": "",
                    "epoch": SOURCE_EPOCH.to_string()
                },
                "options": {
                    "allow_attach_to_existing_attempt": true,
                    "initiator": "User",
                    "should_write_recovery": true,
                },
                "url": "fuchsia-pkg://fuchsia.com/update",
                "state": {
                    "id": "reboot",
                    "info": {
                        "download_size": 0,
                    },
                    "progress": {
                        "bytes_downloaded": 0,
                        "fraction_completed": 1.0,
                    },
                },
            },
            {
                "source": {
                    "update_hash": "",
                    "system_image_hash": "",
                    "vbmeta_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    "zbi_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    "build_version": "",
                    "epoch": SOURCE_EPOCH.to_string()
                },
                "target": {
                    "update_hash": "",
                    "system_image_hash": "",
                    "vbmeta_hash": "",
                    "zbi_hash": "",
                    "build_version": "",
                    "epoch": "",
                },
                "options": {
                    "allow_attach_to_existing_attempt": false,
                    "initiator": "Service",
                    "should_write_recovery": true,
                },
                "url": "fuchsia-pkg://fuchsia.com/not-found",
                "state": {
                    "id": "fail_prepare",
                    "reason": "internal",
                },
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
                    "system_image_hash": "",
                    "vbmeta_hash": "",
                    "zbi_hash": "",
                    "build_version": "",
                    "epoch": SOURCE_EPOCH.to_string()
                },
                "target": {
                    "update_hash": "",
                    "system_image_hash": "",
                    "vbmeta_hash": "",
                    "zbi_hash": "",
                    "build_version": "",
                    "epoch": SOURCE_EPOCH.to_string()
                },
                "options": {
                    "allow_attach_to_existing_attempt": false,
                    "initiator": "User",
                    "should_write_recovery": true,
                },
                "url": "fuchsia-pkg://fuchsia.com/second-attempt",
                "start": 20,
                "state": {
                    "id": "complete",
                    "info": {
                        "download_size": 0,
                    },
                    "progress": {
                        "bytes_downloaded": 0,
                        "fraction_completed": 1.0,
                    },
                },
            },
            {
                "id": "0",
                "source": {
                    "update_hash": "",
                    "system_image_hash": "",
                    "vbmeta_hash": "",
                    "zbi_hash": "",
                    "build_version": "",
                    "epoch": SOURCE_EPOCH.to_string()
                },
                "target": {
                    "update_hash": "",
                    "system_image_hash": "",
                    "vbmeta_hash": "",
                    "zbi_hash": "",
                    "build_version": "",
                    "epoch": SOURCE_EPOCH.to_string()
                },
                "options": {
                    "allow_attach_to_existing_attempt": false,
                    "initiator": "User",
                    "should_write_recovery": true,
                },
                "url": "fuchsia-pkg://fuchsia.com/first-attempt",
                "start": 10,
                "state": {
                    "id": "fetch",
                    "info": {
                        "download_size": 42,
                    },
                    "progress": {
                        "bytes_downloaded": 36,
                        "fraction_completed": 0.8,
                    },
                },
            }
            ],
        }))
        .build()
        .await;

    let installer_proxy = env.installer_proxy();

    assert_eq!(
        installer_proxy.get_last_update_result().await.unwrap(),
        UpdateResult {
            attempt_id: Some("1".to_string()),
            url: Some(PackageUrl { url: "fuchsia-pkg://fuchsia.com/second-attempt".to_string() }),
            options: Some(Options {
                initiator: Some(fidl_fuchsia_update_installer::Initiator::User,),
                allow_attach_to_existing_attempt: Some(false),
                should_write_recovery: Some(true),
                ..Options::EMPTY
            }),
            state: Some(State::Complete(CompleteData {
                info: Some(UpdateInfo { download_size: None, ..UpdateInfo::EMPTY }),
                progress: Some(InstallationProgress {
                    fraction_completed: Some(1.0),
                    bytes_downloaded: None,
                    ..InstallationProgress::EMPTY
                }),
                ..CompleteData::EMPTY
            })),
            ..UpdateResult::EMPTY
        }
    );
    assert_eq!(
        installer_proxy.get_update_result("0").await.unwrap(),
        UpdateResult {
            attempt_id: Some("0".to_string()),
            url: Some(PackageUrl { url: "fuchsia-pkg://fuchsia.com/first-attempt".to_string() }),
            options: Some(Options {
                initiator: Some(fidl_fuchsia_update_installer::Initiator::User),
                allow_attach_to_existing_attempt: Some(false),
                should_write_recovery: Some(true),
                ..Options::EMPTY
            }),
            state: Some(State::Fetch(FetchData {
                info: Some(UpdateInfo { download_size: Some(42), ..UpdateInfo::EMPTY }),
                progress: Some(InstallationProgress {
                    fraction_completed: Some(0.8),
                    bytes_downloaded: Some(36),
                    ..InstallationProgress::EMPTY
                }),
                ..FetchData::EMPTY
            })),
            ..UpdateResult::EMPTY
        }
    );
    assert_eq!(
        installer_proxy.get_update_result("1").await.unwrap(),
        UpdateResult {
            attempt_id: Some("1".to_string()),
            url: Some(PackageUrl { url: "fuchsia-pkg://fuchsia.com/second-attempt".to_string() }),
            options: Some(Options {
                initiator: Some(fidl_fuchsia_update_installer::Initiator::User),
                allow_attach_to_existing_attempt: Some(false),
                should_write_recovery: Some(true),
                ..Options::EMPTY
            }),
            state: Some(State::Complete(CompleteData {
                info: Some(UpdateInfo { download_size: None, ..UpdateInfo::EMPTY }),
                progress: Some(InstallationProgress {
                    fraction_completed: Some(1.0),
                    bytes_downloaded: None,
                    ..InstallationProgress::EMPTY
                }),
                ..CompleteData::EMPTY
            })),
            ..UpdateResult::EMPTY
        }
    );
}

/// When there's no history, the history FIDL APIs should return results with empty fields.
#[fasync::run_singlethreaded(test)]
async fn serves_fidl_without_history_present() {
    let env = TestEnv::new().await;

    let installer_proxy = env.installer_proxy();

    assert_eq!(
        installer_proxy.get_last_update_result().await.unwrap(),
        UpdateResult {
            attempt_id: None,
            url: None,
            options: None,
            state: None,
            ..UpdateResult::EMPTY
        }
    );
    assert_eq!(
        installer_proxy.get_update_result("0").await.unwrap(),
        UpdateResult {
            attempt_id: None,
            url: None,
            options: None,
            state: None,
            ..UpdateResult::EMPTY
        }
    );
}
