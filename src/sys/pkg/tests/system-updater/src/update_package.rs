// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    fidl_fuchsia_pkg::ResolveError,
    fidl_fuchsia_update_installer_ext::{PrepareFailureReason, State},
    maplit::btreemap,
    pretty_assertions::assert_eq,
};

#[fasync::run_singlethreaded(test)]
async fn rejects_invalid_package_name_v1() {
    let env = TestEnv::builder().build().await;

    // Name the update package something other than "update" and assert that the process fails to
    // validate the update package.
    env.resolver
        .register_custom_package("not_update", "not_update", "upd4t3", "fuchsia.com")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("zbi", "fake zbi")
        .add_file("recovery", "new recovery")
        .add_file("version", "build version");

    let not_update_package_url = "fuchsia-pkg://fuchsia.com/not_update";

    let result = env.run_update_with_options(not_update_package_url, default_options()).await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    // Expect to have failed prior to downloading images.
    // The overall result should be similar to an invalid board, and we should have used
    // the not_update package URL, not `fuchsia.com/update`.
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
            PackageResolve(not_update_package_url.to_string())
        ]
    );

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
}

#[fasync::run_singlethreaded(test)]
async fn rejects_invalid_package_name() {
    let env = TestEnv::builder().build().await;

    // Name the update package something other than "update" and assert that the process fails to
    // validate the update package.
    env.resolver
        .register_custom_package("not_update", "not_update", "upd4t3", "fuchsia.com")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("images.json", make_images_json_zbi())
        .add_file("version", "build version");

    let not_update_package_url = "fuchsia-pkg://fuchsia.com/not_update";

    let result = env.run_update_with_options(not_update_package_url, default_options()).await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    // Expect to have failed prior to downloading images.
    // The overall result should be similar to an invalid board, and we should have used
    // the not_update package URL, not `fuchsia.com/update`.
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
            PackageResolve(not_update_package_url.to_string())
        ]
    );

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
}

#[fasync::run_singlethreaded(test)]
async fn fails_if_package_unavailable() {
    let env = TestEnv::builder().build().await;

    env.resolver.mock_resolve_failure(UPDATE_PKG_URL, ResolveError::PackageNotFound);

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
async fn uses_custom_update_package_v1() {
    let env = TestEnv::builder().build().await;

    env.resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("zbi", "fake zbi");

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
    ];

    assert_eq!(env.take_interactions(), events);
}

#[fasync::run_singlethreaded(test)]
async fn uses_custom_update_package() {
    let env = TestEnv::builder().build().await;

    env.resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("images.json", make_images_json_zbi());

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
async fn ignores_malformed_images_manifest_update_package_v1() {
    let env_with_bad_images_json = TestEnv::builder().build().await;

    // if images.json is malformed, do not proceed down the path of using it!
    env_with_bad_images_json
        .resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("zbi", "fake zbi")
        .add_file("images.json", "fake manifest");

    env_with_bad_images_json
        .run_update_with_options("fuchsia-pkg://fuchsia.com/another-update/4", default_options())
        .await
        .expect("run system updater");

    let env_no_images_json = TestEnv::builder().build().await;

    env_no_images_json
        .resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("zbi", "fake zbi");

    env_no_images_json
        .run_update_with_options("fuchsia-pkg://fuchsia.com/another-update/4", default_options())
        .await
        .expect("run system updater");

    assert_eq!(
        env_with_bad_images_json.take_interactions(),
        env_no_images_json.take_interactions(),
    );
}

#[fasync::run_singlethreaded(test)]
async fn retry_update_package_resolve_once_v1() {
    let env = TestEnv::builder().build().await;

    env.resolver.url(UPDATE_PKG_URL).respond_serially(vec![
        // First resolve should fail with NoSpace.
        Err(ResolveError::NoSpace),
        // Second resolve should succeed.
        Ok(env
            .resolver
            .package("update", "upd4t3")
            .add_file("packages.json", make_packages_json([]))
            .add_file("epoch.json", make_current_epoch_json())
            .add_file("zbi", "fake zbi")),
    ]);

    env.run_update().await.expect("run system updater");

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
            // First resolve should fail with NoSpace, so we GC and try the resolve again.
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            // Second resolve should succeed!
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
async fn retry_update_package_resolve_once() {
    let env = TestEnv::builder().build().await;

    env.resolver.url(UPDATE_PKG_URL).respond_serially(vec![
        // First resolve should fail with NoSpace.
        Err(ResolveError::NoSpace),
        // Second resolve should succeed.
        Ok(env
            .resolver
            .package("update", "upd4t3")
            .add_file("packages.json", make_packages_json([]))
            .add_file("epoch.json", make_current_epoch_json())
            .add_file("images.json", make_images_json_zbi())),
    ]);

    env.run_update().await.expect("run system updater");

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
            // First resolve should fail with NoSpace, so we GC and try the resolve again.
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            // Second resolve should succeed!
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
async fn retry_update_package_resolve_twice_v1() {
    let env = TestEnv::builder().build().await;

    env.resolver.url(UPDATE_PKG_URL).respond_serially(vec![
        // First two resolves should fail with NoSpace.
        Err(ResolveError::NoSpace),
        Err(ResolveError::NoSpace),
        // Third resolve should succeed.
        Ok(env
            .resolver
            .package("update", "upd4t3")
            .add_file("packages.json", make_packages_json([]))
            .add_file("epoch.json", make_current_epoch_json())
            .add_file("zbi", "fake zbi")),
    ]);

    env.run_update().await.expect("run system updater");

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
            // First resolve should fail with NoSpace, so we GC and try the resolve again.
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            // Second resolve should fail with NoSpace, so we clear the retained packages set then
            // GC and try the resolve again.
            PackageResolve(UPDATE_PKG_URL.to_string()),
            ClearRetainedPackages,
            Gc,
            // Third resolve should succeed!
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
async fn retry_update_package_resolve_twice() {
    let env = TestEnv::builder().build().await;

    env.resolver.url(UPDATE_PKG_URL).respond_serially(vec![
        // First two resolves should fail with NoSpace.
        Err(ResolveError::NoSpace),
        Err(ResolveError::NoSpace),
        // Third resolve should succeed.
        Ok(env
            .resolver
            .package("update", "upd4t3")
            .add_file("packages.json", make_packages_json([]))
            .add_file("epoch.json", make_current_epoch_json())
            .add_file("images.json", make_images_json_zbi())),
    ]);

    env.run_update().await.expect("run system updater");

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
            // First resolve should fail with NoSpace, so we GC and try the resolve again.
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            // Second resolve should fail with NoSpace, so we clear the retained packages set then
            // GC and try the resolve again.
            PackageResolve(UPDATE_PKG_URL.to_string()),
            ClearRetainedPackages,
            Gc,
            // Third resolve should succeed!
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
async fn retry_update_package_resolve_thrice_fails_update_attempt() {
    let env = TestEnv::builder().build().await;

    env.resolver.url(UPDATE_PKG_URL).respond_serially(vec![
        // Each resolve should fail with NoSpace.
        Err(ResolveError::NoSpace),
        Err(ResolveError::NoSpace),
        Err(ResolveError::NoSpace),
    ]);

    let mut attempt = env.start_update().await.unwrap();

    assert_eq!(attempt.next().await.unwrap().unwrap(), State::Prepare);
    assert_eq!(
        attempt.next().await.unwrap().unwrap(),
        State::FailPrepare(PrepareFailureReason::OutOfSpace)
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
            // First resolve should fail with out of space, so we GC and try the resolve again.
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            // Second resolve should fail with out of space, so we clear retained packages set then
            // GC and try the resolve again.
            PackageResolve(UPDATE_PKG_URL.to_string()),
            ClearRetainedPackages,
            Gc,
            // Third resolve should fail with out of space, so the update fails.
            PackageResolve(UPDATE_PKG_URL.to_string()),
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn fully_populated_images_manifest() {
    let images_json = ::update_package::ImagePackagesManifest::builder()
        .recovery_package(
            ::update_package::ImageMetadata::new(
                5,
                hash(1),
                image_package_resource_url("update-images-recovery", 3, "rzbi"),
            ),
            Some(::update_package::ImageMetadata::new(
                5,
                hash(2),
                image_package_resource_url("update-images-recovery", 3, "rvbmeta"),
            )),
        )
        .fuchsia_package(
            ::update_package::ImageMetadata::new(
                5,
                hash(3),
                image_package_resource_url("update-images-fuchsia", 9, "zbi"),
            ),
            Some(::update_package::ImageMetadata::new(
                5,
                hash(4),
                image_package_resource_url("update-images-fuchsia", 9, "vbmeta"),
            )),
        )
        .firmware_package(
            btreemap! {
                "".to_owned() => ::update_package::ImageMetadata::new(5, hash(5), image_package_resource_url("update-images-firmware", 5, "a")),
                "bl2".to_owned() => ::update_package::ImageMetadata::new(5, hash(6), image_package_resource_url("update-images-firmware", 5, "bl2")),
            },
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
            .package("fuchsia", hashstr(8))
            .add_file("zbi", "zbi contents")
            .add_file("vbmeta", "vbmeta contents"),
    );

    env.resolver.url(image_package_url_to_string("update-images-recovery", 3)).resolve(
        &env.resolver
            .package("recovery", hashstr(4))
            .add_file("rzbi", "rzbi contents")
            .add_file("rvbmeta", "rvbmeta contents"),
    );

    env.resolver.url(image_package_url_to_string("update-images-firmware", 5)).resolve(
        &env.resolver
            .package("firmware", hashstr(2))
            .add_file("a", "afirmware")
            .add_file("bl2", "bl2bl2"),
    );

    env.run_update_with_options("fuchsia-pkg://fuchsia.com/another-update/4", default_options())
        .await
        .expect("run system updater");

    let beginning_events = vec![
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
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::B,
            asset: paver::Asset::VerifiedBootMetadata,
        }),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::Recovery,
            asset: paver::Asset::Kernel,
        }),
        Paver(PaverEvent::ReadAsset {
            configuration: paver::Configuration::Recovery,
            asset: paver::Asset::VerifiedBootMetadata,
        }),
        Paver(PaverEvent::ReadFirmware {
            configuration: paver::Configuration::B,
            firmware_type: "".to_string(),
        }),
        Paver(PaverEvent::ReadFirmware {
            configuration: paver::Configuration::B,
            firmware_type: "bl2".to_string(),
        }),
    ];

    // Resolving the images is done nondeterministically.
    // Below we assert that the events still happened.

    let end_events = [
        Paver(PaverEvent::WriteFirmware {
            configuration: paver::Configuration::B,
            firmware_type: "".to_string(),
            payload: b"afirmware".to_vec(),
        }),
        Paver(PaverEvent::WriteFirmware {
            configuration: paver::Configuration::B,
            firmware_type: "bl2".to_string(),
            payload: b"bl2bl2".to_vec(),
        }),
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
        Paver(PaverEvent::WriteAsset {
            configuration: paver::Configuration::Recovery,
            asset: paver::Asset::Kernel,
            payload: b"rzbi contents".to_vec(),
        }),
        Paver(PaverEvent::WriteAsset {
            configuration: paver::Configuration::Recovery,
            asset: paver::Asset::VerifiedBootMetadata,
            payload: b"rvbmeta contents".to_vec(),
        }),
        Paver(PaverEvent::DataSinkFlush),
        ReplaceRetainedPackages(vec![]),
        Gc,
        BlobfsSync,
        Paver(PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B }),
        Paver(PaverEvent::BootManagerFlush),
        Reboot,
    ];

    let all_events = env.take_interactions();

    let all_events_start = all_events[0..beginning_events.len()].to_vec();
    assert_eq!(all_events_start, beginning_events);

    let all_events_end = all_events[all_events.len() - end_events.len()..all_events.len()].to_vec();
    assert_eq!(all_events_end, end_events);

    // The 5 is for the nondeterministic events.
    // ReplaceRetainedPackages (of which we cannot guarantee the order of the image package hashes),
    // a Gc occurs, and the 3 PackageResolves for the image packages happen concurrently.
    assert!(all_events.len() == beginning_events.len() + end_events.len() + 5);

    let all_events_middle = all_events[beginning_events.len()..beginning_events.len() + 5].to_vec();

    // it's got to be one of these combinatorics!
    assert!(
        all_events_middle[0]
            == ReplaceRetainedPackages(vec![hash(9).into(), hash(3).into(), hash(5).into()])
            || all_events_middle[0]
                == ReplaceRetainedPackages(vec![hash(9).into(), hash(5).into(), hash(3).into()])
            || all_events_middle[0]
                == ReplaceRetainedPackages(vec![hash(5).into(), hash(3).into(), hash(9).into()])
            || all_events_middle[0]
                == ReplaceRetainedPackages(vec![hash(5).into(), hash(9).into(), hash(3).into()])
            || all_events_middle[0]
                == ReplaceRetainedPackages(vec![hash(3).into(), hash(5).into(), hash(9).into()])
            || all_events_middle[0]
                == ReplaceRetainedPackages(vec![hash(3).into(), hash(9).into(), hash(5).into()])
    );

    assert!(all_events_middle[1] == Gc);

    assert!(all_events_middle
        .contains(&PackageResolve(image_package_url_to_string("update-images-recovery", 3))));
    assert!(all_events_middle
        .contains(&PackageResolve(image_package_url_to_string("update-images-firmware", 5))));
    assert!(all_events_middle
        .contains(&PackageResolve(image_package_url_to_string("update-images-fuchsia", 9))));
}
