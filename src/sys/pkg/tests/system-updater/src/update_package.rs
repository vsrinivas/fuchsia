// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::*, pretty_assertions::assert_eq};

#[fasync::run_singlethreaded(test)]
async fn rejects_invalid_package_name() {
    let env = TestEnv::new();

    // Name the update package something other than "update" and assert that the process fails to
    // validate the update package.
    env.resolver
        .register_custom_package("not_update", "not_update", "upd4t3", "fuchsia.com")
        .add_file(
            "packages",
            "system_image/0=42ade6f4fd51636f70c68811228b4271ed52c4eb9a647305123b4f4d0741f296\n",
        )
        .add_file("zbi", "fake zbi")
        .add_file("zedboot", "new recovery");

    let not_update_package_url = "fuchsia-pkg://fuchsia.com/not_update";

    let result = env
        .run_system_updater(SystemUpdaterArgs {
            initiator: Some(Initiator::User),
            target: Some("m3rk13"),
            update: Some(not_update_package_url),
            oneshot: Some(true),
            ..Default::default()
        })
        .await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    // Expect to have failed prior to downloading images.
    // The overall result should be similar to an invalid board, and we should have used
    // the not_update package URL, not `fuchsia.com/update`.
    assert_eq!(
        env.take_interactions(),
        vec![Gc, PackageResolve(not_update_package_url.to_string())]
    );

    let loggers = env.logger_factory.loggers.lock().clone();
    assert_eq!(loggers.len(), 1);
    let logger = loggers.into_iter().next().unwrap();
    let events = OtaMetrics::from_events(logger.cobalt_events.lock().clone());
    assert_eq!(
        events,
        OtaMetrics {
            initiator: metrics::OtaResultAttemptsMetricDimensionInitiator::UserInitiatedCheck
                as u32,
            phase: metrics::OtaResultAttemptsMetricDimensionPhase::Tufupdate as u32,
            status_code: metrics::OtaResultAttemptsMetricDimensionStatusCode::Error as u32,
            target: "m3rk13".into(),
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn fails_if_package_unavailable() {
    let env = TestEnv::new();

    env.resolver.mock_resolve_failure(UPDATE_PKG_URL, Status::NOT_FOUND);

    let result = env
        .run_system_updater(SystemUpdaterArgs {
            oneshot: Some(true),
            initiator: Some(Initiator::User),
            target: Some("m3rk13"),
            ..Default::default()
        })
        .await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    assert_eq!(env.take_interactions(), vec![Gc, PackageResolve(UPDATE_PKG_URL.to_string()),]);
}

#[fasync::run_singlethreaded(test)]
async fn packages_json_takes_precedence() {
    let env = TestEnv::new();

    let pkg1_url = "fuchsia-pkg://fuchsia.com/amber/0?hash=00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100";
    let pkg2_url = "fuchsia-pkg://fuchsia.com/pkgfs/0?hash=ffeeddccbbaa9988776655443322110000112233445566778899aabbccddeeff";
    env.resolver
        .register_package("update", "upd4t3")
        .add_file(
            "packages",
            "system_image/0=42ade6f4fd51636f70c68811228b4271ed52c4eb9a647305123b4f4d0741f296\n",
        )
        .add_file("packages.json", make_packages_json([pkg1_url, pkg2_url]))
        .add_file("zbi", "fake zbi");

    env.resolver
        .url(SYSTEM_IMAGE_URL)
        .resolve(&env.resolver.package("system_image/0", SYSTEM_IMAGE_HASH));
    env.resolver.url(pkg1_url).resolve(
        &env.resolver
            .package("amber/0", "00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100"),
    );
    env.resolver.url(pkg2_url).resolve(
        &env.resolver
            .package("pkgfs/0", "ffeeddccbbaa9988776655443322110000112233445566778899aabbccddeeff"),
    );

    env.run_system_updater(SystemUpdaterArgs {
        oneshot: Some(true),
        initiator: Some(Initiator::User),
        target: Some("m3rk13"),
        ..Default::default()
    })
    .await
    .expect("run system updater");

    assert_eq!(
        resolved_urls(Arc::clone(&env.interactions)),
        vec![UPDATE_PKG_URL, pkg1_url, pkg2_url]
    );
}
