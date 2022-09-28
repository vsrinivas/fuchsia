// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::*, pretty_assertions::assert_eq};

async fn test_resolve_error_maps_to_cobalt_status_code(
    error: fidl_fuchsia_pkg::ResolveError,
    expected_status_code: metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode,
) {
    let env = TestEnv::builder().build().await;

    let pkg_url = "fuchsia-pkg://fuchsia.com/failure/0?hash=00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100";

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([pkg_url]))
        .add_file("images.json", make_images_json_zbi())
        .add_file("epoch.json", make_current_epoch_json());

    env.resolver.url(pkg_url).fail(error);

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator:
                metrics::OtaResultAttemptsMigratedMetricDimensionInitiator::UserInitiatedCheck
                    as u32,
            phase: metrics::OtaResultAttemptsMigratedMetricDimensionPhase::PackageDownload as u32,
            status_code: expected_status_code as u32,
        }
    );
}

async fn test_resolve_error_maps_to_cobalt_status_code_v1(
    error: fidl_fuchsia_pkg::ResolveError,
    expected_status_code: metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode,
) {
    let env = TestEnv::builder().build().await;

    let pkg_url = "fuchsia-pkg://fuchsia.com/failure/0?hash=00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100";

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([pkg_url]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("zbi", "fake zbi");

    env.resolver.url(pkg_url).fail(error);

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator:
                metrics::OtaResultAttemptsMigratedMetricDimensionInitiator::UserInitiatedCheck
                    as u32,
            phase: metrics::OtaResultAttemptsMigratedMetricDimensionPhase::PackageDownload as u32,
            status_code: expected_status_code as u32,
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn reports_untrusted_tuf_repo() {
    test_resolve_error_maps_to_cobalt_status_code(
        fidl_fuchsia_pkg::ResolveError::AccessDenied,
        metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode::ErrorUntrustedTufRepo,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn reports_out_of_space() {
    test_resolve_error_maps_to_cobalt_status_code(
        fidl_fuchsia_pkg::ResolveError::NoSpace,
        metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode::ErrorStorageOutOfSpace,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn reports_misc_storage() {
    test_resolve_error_maps_to_cobalt_status_code(
        fidl_fuchsia_pkg::ResolveError::Io,
        metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode::ErrorStorage,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn reports_network() {
    test_resolve_error_maps_to_cobalt_status_code(
        fidl_fuchsia_pkg::ResolveError::UnavailableBlob,
        metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode::ErrorNetworking,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn reports_untrusted_tuf_repo_v1() {
    test_resolve_error_maps_to_cobalt_status_code_v1(
        fidl_fuchsia_pkg::ResolveError::AccessDenied,
        metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode::ErrorUntrustedTufRepo,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn reports_out_of_space_v1() {
    test_resolve_error_maps_to_cobalt_status_code_v1(
        fidl_fuchsia_pkg::ResolveError::NoSpace,
        metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode::ErrorStorageOutOfSpace,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn reports_misc_storage_v1() {
    test_resolve_error_maps_to_cobalt_status_code_v1(
        fidl_fuchsia_pkg::ResolveError::Io,
        metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode::ErrorStorage,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn reports_network_v1() {
    test_resolve_error_maps_to_cobalt_status_code_v1(
        fidl_fuchsia_pkg::ResolveError::UnavailableBlob,
        metrics::OtaResultAttemptsMigratedMetricDimensionStatusCode::ErrorNetworking,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn succeeds_even_if_metrics_fail_to_send_v1() {
    let env = TestEnvBuilder::new().unregister_protocol(Protocol::FuchsiaMetrics).build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_current_epoch_json())
        .add_file("zbi", "fake zbi");

    env.run_update().await.expect("run system updater");

    let loggers = env.metric_event_logger_factory.clone_loggers();
    assert_eq!(loggers.len(), 0);

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
async fn succeeds_even_if_metrics_fail_to_send() {
    let env = TestEnvBuilder::new().unregister_protocol(Protocol::FuchsiaMetrics).build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("images.json", make_images_json_zbi())
        .add_file("epoch.json", make_current_epoch_json());

    env.run_update().await.expect("run system updater");

    let loggers = env.metric_event_logger_factory.clone_loggers();
    assert_eq!(loggers.len(), 0);

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
