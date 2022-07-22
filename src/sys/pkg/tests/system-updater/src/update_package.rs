// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    ::update_package as lib_update_package,
    fidl_fuchsia_pkg::ResolveError,
    fidl_fuchsia_update_installer_ext::{PrepareFailureReason, State},
    fuchsia_hash::Hash,
    fuchsia_url::AbsoluteComponentUrl,
    maplit::btreemap,
    pretty_assertions::assert_eq,
    std::str::FromStr,
};

fn image_package_resource_url(name: &str, hash: u8, resource: &str) -> AbsoluteComponentUrl {
    format!("fuchsia-pkg://fuchsia.com/{name}/0?hash={}#{resource}", hashstr(hash)).parse().unwrap()
}

fn hash(n: u8) -> Hash {
    Hash::from([n; 32])
}

fn hashstr(n: u8) -> String {
    hash(n).to_string()
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
        ReplaceRetainedPackages(vec![]),
        Gc,
    ];

    let mut postscript = vec![
        Paver(PaverEvent::WriteAsset {
            configuration: paver::Configuration::B,
            asset: paver::Asset::Kernel,
            payload: b"fake zbi".to_vec(),
        }),
        Paver(PaverEvent::DataSinkFlush),
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
async fn rejects_invalid_package_name() {
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
            initiator: metrics::OtaResultAttemptsMetricDimensionInitiator::UserInitiatedCheck
                as u32,
            phase: metrics::OtaResultAttemptsMetricDimensionPhase::Tufupdate as u32,
            status_code: metrics::OtaResultAttemptsMetricDimensionStatusCode::Error as u32,
            target: "build version".into(),
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
async fn uses_custom_update_package() {
    let env = TestEnv::builder().build().await;

    env.resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
        .add_file("zbi", "fake zbi");

    env.run_update_with_options("fuchsia-pkg://fuchsia.com/another-update/4", default_options())
        .await
        .expect("run system updater");

    let mut events = vec![];

    assert_eq!(env.take_interactions(), construct_events(&mut events));
}

#[fasync::run_singlethreaded(test)]
async fn ignores_malformed_images_manifest_update_package() {
    let env_with_bad_images_json = TestEnv::builder().build().await;

    env_with_bad_images_json
        .resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
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
        .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
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
async fn images_manifest_update_package_firmware_no_match() {
    let images_json = lib_update_package::ImagePackagesManifest::builder()
        .firmware_package(
                btreemap! {
                    "".to_owned() => lib_update_package::ImageMetadata::new(5, hash(5), image_package_resource_url("update-images-firmware", 6, "a")
                ),
                    "bl2".to_owned() => lib_update_package::ImageMetadata::new(6, hash(6), image_package_resource_url("update-images-firmware", 5, "b")
                ),
                },
            )
        .clone()
        .build();

    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder.insert_hook(mphooks::read_firmware(|configuration, type_| {
                match (type_.as_str(), configuration) {
                    ("bl2", _) => Ok(b"not a match here".to_vec()),
                    (_, _) => Ok(b"not a match".to_vec()),
                }
            }))
        })
        .build()
        .await;

    env.resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
        .add_file("zbi", "fake zbi")
        .add_file("images.json", serde_json::to_string(&images_json).unwrap());

    env.run_update_with_options("fuchsia-pkg://fuchsia.com/another-update/4", default_options())
        .await
        .expect("run system updater");

    let mut events = vec![
        Paver(PaverEvent::ReadFirmware {
            configuration: paver::Configuration::B,
            firmware_type: "".to_string(),
        }),
        Paver(PaverEvent::ReadFirmware {
            configuration: paver::Configuration::A,
            firmware_type: "".to_string(),
        }),
        Paver(PaverEvent::ReadFirmware {
            configuration: paver::Configuration::B,
            firmware_type: "bl2".to_string(),
        }),
        Paver(PaverEvent::ReadFirmware {
            configuration: paver::Configuration::A,
            firmware_type: "bl2".to_string(),
        }),
    ];

    assert_eq!(env.take_interactions(), construct_events(&mut events));
}

#[fasync::run_singlethreaded(test)]
async fn images_manifest_update_package_firmware_match_desired_config() {
    let images_json = lib_update_package::ImagePackagesManifest::builder()
        .firmware_package(
                btreemap! {
                    "".to_owned() => lib_update_package::ImageMetadata::new(8, Hash::from_str("e0705e68b0468289858b543f8a57f375a3b4f46391a72f94a28d82d6a3dacaa7").unwrap(), image_package_resource_url("update-images-firmware", 6, "a")
                ),
                },
            )
        .clone()
        .build();

    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder.insert_hook(mphooks::read_firmware(|_, _| {
                return Ok(b"matching".to_vec());
            }))
        })
        .build()
        .await;

    env.resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
        .add_file("zbi", "fake zbi")
        .add_file("images.json", serde_json::to_string(&images_json).unwrap());

    env.run_update_with_options("fuchsia-pkg://fuchsia.com/another-update/4", default_options())
        .await
        .expect("run system updater");

    let mut events = vec![Paver(PaverEvent::ReadFirmware {
        configuration: paver::Configuration::B,
        firmware_type: "".to_string(),
    })];

    assert_eq!(env.take_interactions(), construct_events(&mut events));
}

#[fasync::run_singlethreaded(test)]
async fn images_manifest_update_package_firmware_match_active_config() {
    let images_json = lib_update_package::ImagePackagesManifest::builder()
        .firmware_package(
                btreemap! {
                    "".to_owned() => lib_update_package::ImageMetadata::new(8, Hash::from_str("e0705e68b0468289858b543f8a57f375a3b4f46391a72f94a28d82d6a3dacaa7").unwrap(), image_package_resource_url("update-images-firmware", 6, "a")
                ),
                },
            )
        .clone()
        .build();

    let env = TestEnv::builder()
        .paver_service(|builder| {
            builder.insert_hook(mphooks::read_firmware(|configuration, _| match configuration {
                paver::Configuration::A => Ok(b"matching".to_vec()),
                _ => Ok(b"no match".to_vec()),
            }))
        })
        .build()
        .await;

    env.resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
        .add_file("zbi", "fake zbi")
        .add_file("images.json", serde_json::to_string(&images_json).unwrap());

    env.run_update_with_options("fuchsia-pkg://fuchsia.com/another-update/4", default_options())
        .await
        .expect("run system updater");

    let mut events = vec![
        Paver(PaverEvent::ReadFirmware {
            configuration: paver::Configuration::B,
            firmware_type: "".to_string(),
        }),
        Paver(PaverEvent::ReadFirmware {
            configuration: paver::Configuration::A,
            firmware_type: "".to_string(),
        }),
        Paver(PaverEvent::WriteFirmware {
            configuration: paver::Configuration::B,
            firmware_type: "".to_string(),
            payload: b"matching".to_vec(),
        }),
    ];
    assert_eq!(env.take_interactions(), construct_events(&mut events));
}

#[fasync::run_singlethreaded(test)]
async fn images_manifest_update_package_zbi_no_match() {
    let images_json = lib_update_package::ImagePackagesManifest::builder()
        .fuchsia_package(
            lib_update_package::ImageMetadata::new(
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
        .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
        .add_file("zbi", "fake zbi")
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
    ];
    assert_eq!(env.take_interactions(), construct_events(&mut events));
}

#[fasync::run_singlethreaded(test)]
async fn images_manifest_update_package_zbi_match_in_desired_config() {
    let images_json = lib_update_package::ImagePackagesManifest::builder()
        .fuchsia_package(
            lib_update_package::ImageMetadata::new(
                8,
                Hash::from_str("e0705e68b0468289858b543f8a57f375a3b4f46391a72f94a28d82d6a3dacaa7")
                    .unwrap(),
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
        .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
        .add_file("zbi", "fake zbi")
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
async fn images_manifest_update_package_zbi_match_in_active_config() {
    let images_json = lib_update_package::ImagePackagesManifest::builder()
        .fuchsia_package(
            lib_update_package::ImageMetadata::new(
                8,
                Hash::from_str("e0705e68b0468289858b543f8a57f375a3b4f46391a72f94a28d82d6a3dacaa7")
                    .unwrap(),
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
        .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
        .add_file("zbi", "fake zbi")
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
async fn images_manifest_update_package_zbi_match_in_active_config_error_in_desired_config() {
    let images_json = lib_update_package::ImagePackagesManifest::builder()
        .fuchsia_package(
            lib_update_package::ImageMetadata::new(
                8,
                Hash::from_str("e0705e68b0468289858b543f8a57f375a3b4f46391a72f94a28d82d6a3dacaa7")
                    .unwrap(),
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
        .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
        .add_file("zbi", "fake zbi")
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
async fn images_manifest_update_package_recovery_already_present() {
    let images_json = lib_update_package::ImagePackagesManifest::builder()
        .recovery_package(
            lib_update_package::ImageMetadata::new(
                8,
                Hash::from_str("e0705e68b0468289858b543f8a57f375a3b4f46391a72f94a28d82d6a3dacaa7")
                    .unwrap(),
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
        .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
        .add_file("zbi", "fake zbi")
        .add_file("images.json", serde_json::to_string(&images_json).unwrap());

    env.run_update_with_options("fuchsia-pkg://fuchsia.com/another-update/4", default_options())
        .await
        .expect("run system updater");

    let mut events = vec![Paver(PaverEvent::ReadAsset {
        configuration: paver::Configuration::Recovery,
        asset: paver::Asset::Kernel,
    })];
    assert_eq!(env.take_interactions(), construct_events(&mut events));
}

#[fasync::run_singlethreaded(test)]
async fn images_manifest_update_package_recovery_no_match() {
    let images_json = lib_update_package::ImagePackagesManifest::builder()
        .recovery_package(
            lib_update_package::ImageMetadata::new(
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
        .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
        .add_file("zbi", "fake zbi")
        .add_file("images.json", serde_json::to_string(&images_json).unwrap());

    env.run_update_with_options("fuchsia-pkg://fuchsia.com/another-update/4", default_options())
        .await
        .expect("run system updater");
    let mut events = vec![Paver(PaverEvent::ReadAsset {
        configuration: paver::Configuration::Recovery,
        asset: paver::Asset::Kernel,
    })];
    assert_eq!(env.take_interactions(), construct_events(&mut events));
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
            .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
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
            ReplaceRetainedPackages(vec![]),
            Gc,
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::B,
                asset: paver::Asset::Kernel,
                payload: b"fake zbi".to_vec(),
            }),
            Paver(PaverEvent::DataSinkFlush),
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
            .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
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
            ReplaceRetainedPackages(vec![]),
            Gc,
            Paver(PaverEvent::WriteAsset {
                configuration: paver::Configuration::B,
                asset: paver::Asset::Kernel,
                payload: b"fake zbi".to_vec(),
            }),
            Paver(PaverEvent::DataSinkFlush),
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
