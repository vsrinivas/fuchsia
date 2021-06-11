// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::*, pretty_assertions::assert_eq};

async fn test_resolve_error_maps_to_cobalt_status_code(
    error: fidl_fuchsia_pkg::ResolveError,
    expected_status_code: metrics::OtaResultAttemptsMetricDimensionStatusCode,
) {
    let env = TestEnv::builder().build().await;

    let pkg_url = "fuchsia-pkg://fuchsia.com/failure/0?hash=00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100";

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([pkg_url]))
        .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
        .add_file("zbi", "fake zbi");

    env.resolver.url(pkg_url).fail(error);

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(
        env.get_ota_metrics().await,
        OtaMetrics {
            initiator: metrics::OtaResultAttemptsMetricDimensionInitiator::UserInitiatedCheck
                as u32,
            phase: metrics::OtaResultAttemptsMetricDimensionPhase::PackageDownload as u32,
            status_code: expected_status_code as u32,
            target: "".into(),
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn reports_untrusted_tuf_repo() {
    test_resolve_error_maps_to_cobalt_status_code(
        fidl_fuchsia_pkg::ResolveError::AccessDenied,
        metrics::OtaResultAttemptsMetricDimensionStatusCode::ErrorUntrustedTufRepo,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn reports_out_of_space() {
    test_resolve_error_maps_to_cobalt_status_code(
        fidl_fuchsia_pkg::ResolveError::NoSpace,
        metrics::OtaResultAttemptsMetricDimensionStatusCode::ErrorStorageOutOfSpace,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn reports_misc_storage() {
    test_resolve_error_maps_to_cobalt_status_code(
        fidl_fuchsia_pkg::ResolveError::Io,
        metrics::OtaResultAttemptsMetricDimensionStatusCode::ErrorStorage,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn reports_network() {
    test_resolve_error_maps_to_cobalt_status_code(
        fidl_fuchsia_pkg::ResolveError::UnavailableBlob,
        metrics::OtaResultAttemptsMetricDimensionStatusCode::ErrorNetworking,
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn succeeds_even_if_metrics_fail_to_send() {
    let env = TestEnvBuilder::new().unregister_protocol(Protocol::Cobalt).build().await;

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
        .add_file("zbi", "fake zbi");

    env.run_update().await.expect("run system updater");

    let loggers = env.logger_factory.loggers.lock().clone();
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
