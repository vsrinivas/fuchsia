// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::*, pretty_assertions::assert_eq};

#[fasync::run_singlethreaded(test)]
async fn rejects_invalid_package_name() {
    let env = TestEnv::builder().build();

    // Name the update package something other than "update" and assert that the process fails to
    // validate the update package.
    env.resolver
        .register_custom_package("not_update", "not_update", "upd4t3", "fuchsia.com")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
        .add_file("zbi", "fake zbi")
        .add_file("zedboot", "new recovery")
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
            Paver(PaverEvent::QueryActiveConfiguration),
            Gc,
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
    let env = TestEnv::builder().build();

    env.resolver.mock_resolve_failure(UPDATE_PKG_URL, Status::NOT_FOUND);

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
            Paver(PaverEvent::QueryActiveConfiguration),
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn uses_custom_update_package() {
    let env = TestEnv::builder().build();

    env.resolver
        .register_custom_package("another-update/4", "update", "upd4t3r", "fuchsia.com")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi");

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
            Paver(PaverEvent::QueryActiveConfiguration),
            Gc,
            PackageResolve("fuchsia-pkg://fuchsia.com/another-update/4".to_string()),
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
