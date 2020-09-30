// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    crate::{merkle_str, pinned_pkg_url},
    pretty_assertions::assert_eq,
};

#[fasync::run_singlethreaded(test)]
async fn fails_on_package_resolver_connect_error() {
    let env =
        TestEnvBuilder::new().unregister_protocol(Protocol::PackageResolver).oneshot(true).build();

    let result = env
        .run_system_updater_oneshot(SystemUpdaterArgs {
            initiator: Some(Initiator::User),
            target: Some("m3rk13"),
            ..Default::default()
        })
        .await;
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
            // The connect succeeds, so the system updater only notices the resolver is not there when
            // it tries to resolve a package
            Gc
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn fails_on_update_package_fetch_error() {
    let env = TestEnv::builder().oneshot(true).build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]));

    let system_image_url = SYSTEM_IMAGE_URL;
    env.resolver.mock_resolve_failure(system_image_url, Status::NOT_FOUND);

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
            phase: metrics::OtaResultAttemptsMetricDimensionPhase::PackageDownload as u32,
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
            PackageResolve(system_image_url.to_string()),
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn fails_on_content_package_fetch_error() {
    let env = TestEnv::builder().oneshot(true).build();

    let pkg1_url = pinned_pkg_url!("package1/0", "aa");
    let pkg2_url = pinned_pkg_url!("package2/0", "00");
    let pkg3_url = pinned_pkg_url!("package3/0", "bb");
    let pkg4_url = pinned_pkg_url!("package4/0", "cc");
    let pkg5_url = pinned_pkg_url!("package5/0", "dd");
    let pkg6_url = pinned_pkg_url!("package6/0", "ee");

    let pkg1 = env.resolver.package("package1", merkle_str!("aa"));
    let pkg3 = env.resolver.package("package3", merkle_str!("bb"));
    let pkg4 = env.resolver.package("package4", merkle_str!("cc"));
    let pkg5 = env.resolver.package("package5", merkle_str!("dd"));

    env.resolver.url("fuchsia-pkg://fuchsia.com/update").resolve(
        &env.resolver.package("update", UPDATE_HASH).add_file(
            "packages.json",
            make_packages_json([
                SYSTEM_IMAGE_URL,
                pkg1_url,
                pkg2_url,
                pkg3_url,
                pkg4_url,
                pkg5_url,
                pkg6_url,
            ]),
        ),
    );
    env.resolver
        .url(SYSTEM_IMAGE_URL)
        .resolve(&env.resolver.package("system_image", SYSTEM_IMAGE_HASH));
    let mut handle_pkg1 = env.resolver.url(pkg1_url).block_once();
    let mut handle_pkg2 = env.resolver.url(pkg2_url).block_once();
    let mut handle_pkg3 = env.resolver.url(pkg3_url).block_once();
    let mut handle_pkg4 = env.resolver.url(pkg4_url).block_once();
    let mut handle_pkg5 = env.resolver.url(pkg5_url).block_once();
    env.resolver.url(pkg6_url).resolve(&env.resolver.package("package6", merkle_str!("ee")));

    let ((), ()) = future::join(
        async {
            let result = env
                .run_system_updater_oneshot(SystemUpdaterArgs {
                    initiator: Some(Initiator::User),
                    target: Some("m3rk13"),
                    ..Default::default()
                })
                .await;
            assert!(result.is_err(), "system updater succeeded when it should fail");
        },
        async move {
            // The system updater will try to resolve 5 packages concurrently. Wait for the
            // requests to come in.
            handle_pkg1.wait().await;
            handle_pkg2.wait().await;
            handle_pkg3.wait().await;
            handle_pkg4.wait().await;
            handle_pkg5.wait().await;

            // Return a failure for pkg2, and success for the other blocked packages.
            handle_pkg2.fail(Status::NOT_FOUND).await;

            handle_pkg1.resolve(&pkg1).await;
            handle_pkg3.resolve(&pkg3).await;
            handle_pkg4.resolve(&pkg4).await;
            handle_pkg5.resolve(&pkg5).await;
        },
    )
    .await;

    let loggers = env.logger_factory.loggers.lock().clone();
    assert_eq!(loggers.len(), 1);
    let logger = loggers.into_iter().next().unwrap();
    assert_eq!(
        OtaMetrics::from_events(logger.cobalt_events.lock().clone()),
        OtaMetrics {
            initiator: metrics::OtaResultAttemptsMetricDimensionInitiator::UserInitiatedCheck
                as u32,
            phase: metrics::OtaResultAttemptsMetricDimensionPhase::PackageDownload as u32,
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
            PackageResolve(SYSTEM_IMAGE_URL.to_string()),
            PackageResolve(pkg1_url.to_string()),
            PackageResolve(pkg2_url.to_string()),
            PackageResolve(pkg3_url.to_string()),
            PackageResolve(pkg4_url.to_string()),
            PackageResolve(pkg5_url.to_string()),
            // pkg6_url is never resolved, as pkg2's resolve finishes first with an error.
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn fails_when_package_cache_sync_fails() {
    let env = TestEnv::builder().oneshot(true).build();
    env.cache_service.set_sync_response(Err(Status::INTERNAL));
    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]));
    env.resolver
        .url(SYSTEM_IMAGE_URL)
        .resolve(&env.resolver.package("system_image/0", SYSTEM_IMAGE_HASH));

    let result = env
        .run_system_updater_oneshot(SystemUpdaterArgs {
            initiator: Some(Initiator::User),
            target: Some("m3rk13"),
            ..Default::default()
        })
        .await;

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
            Gc,
            PackageResolve(SYSTEM_IMAGE_URL.to_string()),
            BlobfsSync,
        ]
    );
}
