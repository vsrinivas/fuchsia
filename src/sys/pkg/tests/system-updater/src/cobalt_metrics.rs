// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::*, pretty_assertions::assert_eq};

#[fasync::run_singlethreaded(test)]
async fn reports_untrusted_tuf_repo() {
    let env = TestEnv::new();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file(
            "packages.json",
            &json!({
              // TODO: Change to "1" once we remove support for versions as ints.
              "version": 1,
              "content": [
                "fuchsia-pkg://non-existent-repo.com/amber/0?hash=00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100",
                "fuchsia-pkg://fuchsia.com/pkgfs/0?hash=ffeeddccbbaa9988776655443322110000112233445566778899aabbccddeeff",
              ]
            })
            .to_string(),
        )
        .add_file("zbi", "fake zbi");

    env.resolver.mock_resolve_failure(
        "fuchsia-pkg://non-existent-repo.com/amber/0?hash=00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100",
        Status::ADDRESS_UNREACHABLE,
    );

    let result = env
        .run_system_updater(SystemUpdaterArgs {
            initiator: "manual",
            target: "m3rk13",
            update: None,
            reboot: None,
            skip_recovery: None,
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
            phase: metrics::OtaResultAttemptsMetricDimensionPhase::PackageDownload as u32,
            status_code: metrics::OtaResultAttemptsMetricDimensionStatusCode::ErrorUntrustedTufRepo
                as u32,
            target: "m3rk13".into(),
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn succeeds_even_if_metrics_fail_to_send() {
    let env = TestEnv::new();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file(
            "packages",
            "system_image/0=42ade6f4fd51636f70c68811228b4271ed52c4eb9a647305123b4f4d0741f296\n",
        )
        .add_file("zbi", "fake zbi");

    *env.logger_factory.broken.lock() = true;

    env.run_system_updater(SystemUpdaterArgs {
        initiator: "manual",
        target: "m3rk13",
        update: None,
        reboot: None,
        skip_recovery: None,
    })
    .await
    .expect("run system updater");

    let loggers = env.logger_factory.loggers.lock().clone();
    assert_eq!(loggers.len(), 0);

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
