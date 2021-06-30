// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::*, fidl_fuchsia_pkg::ResolveError, pretty_assertions::assert_eq};

#[fasync::run_singlethreaded(test)]
async fn succeeds_even_if_retained_packages_fails() {
    let env =
        TestEnv::builder().unregister_protocol(crate::Protocol::RetainedPackages).build().await;

    env.resolver.url(UPDATE_PKG_URL_PINNED).respond_serially(vec![
        // To trigger first RetainedPackages use, first two resolves of update package must fail.
        Err(ResolveError::NoSpace),
        Err(ResolveError::NoSpace),
        Ok(env
            .resolver
            .package("update", UPDATE_HASH)
            .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
            .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
            .add_file("zbi", "fake zbi")),
    ]);

    env.resolver
        .url(SYSTEM_IMAGE_URL)
        .resolve(&env.resolver.package("system_image/0", SYSTEM_IMAGE_HASH));

    let () = env
        .run_update_with_options(UPDATE_PKG_URL_PINNED, default_options())
        .await
        .expect("run system updater");

    assert_eq!(
        env.take_interactions(),
        vec![
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
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::B
            }),
            Paver(PaverEvent::BootManagerFlush),
            PackageResolve(UPDATE_PKG_URL_PINNED.to_string()),
            Gc,
            PackageResolve(UPDATE_PKG_URL_PINNED.to_string()),
            // No RetainedPackages use here b/c protocol is blocked
            Gc,
            PackageResolve(UPDATE_PKG_URL_PINNED.to_string()),
            // No RetainedPackages use here b/c protocol is blocked
            Gc,
            PackageResolve(SYSTEM_IMAGE_URL.to_string()),
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

// Verifies that:
//   1. pinned update pkg url causes both RetainedPackages uses to include the update package
//   2. second RetainedPackages use includes packages from packages.json
#[fasync::run_singlethreaded(test)]
async fn pinned_url_and_non_empty_packages_json() {
    let env = TestEnv::builder().build().await;

    env.resolver.url(UPDATE_PKG_URL_PINNED).respond_serially(vec![
        // To trigger first RetainedPackages use, first two resolves of update package must fail.
        Err(ResolveError::NoSpace),
        Err(ResolveError::NoSpace),
        Ok(env
            .resolver
            .package("update", UPDATE_HASH)
            .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
            .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
            .add_file("zbi", "fake zbi")),
    ]);

    env.resolver
        .url(SYSTEM_IMAGE_URL)
        .resolve(&env.resolver.package("system_image/0", SYSTEM_IMAGE_HASH));

    let () = env
        .run_update_with_options(UPDATE_PKG_URL_PINNED, default_options())
        .await
        .expect("run system updater");

    assert_eq!(
        env.take_interactions(),
        vec![
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
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::B
            }),
            Paver(PaverEvent::BootManagerFlush),
            PackageResolve(UPDATE_PKG_URL_PINNED.to_string()),
            Gc,
            PackageResolve(UPDATE_PKG_URL_PINNED.to_string()),
            ReplaceRetainedPackages(vec![UPDATE_HASH.parse().unwrap()]),
            Gc,
            PackageResolve(UPDATE_PKG_URL_PINNED.to_string()),
            ReplaceRetainedPackages(vec![
                SYSTEM_IMAGE_HASH.parse().unwrap(),
                UPDATE_HASH.parse().unwrap()
            ]),
            Gc,
            PackageResolve(SYSTEM_IMAGE_URL.to_string()),
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

// Verifies that:
//   1. unpinned update pkg url causes first RetainedPackages use to be Clear and second use to
//      not include the update package
//   2. second RetainedPackages use still includes packages from packages.json
#[fasync::run_singlethreaded(test)]
async fn unpinned_url_and_non_empty_packages_json() {
    let env = TestEnv::builder().build().await;

    env.resolver.url(UPDATE_PKG_URL).respond_serially(vec![
        // To trigger first RetainedPackages use, first two resolves of update package must fail.
        Err(ResolveError::NoSpace),
        Err(ResolveError::NoSpace),
        Ok(env
            .resolver
            .package("update", UPDATE_HASH)
            .add_file("packages.json", make_packages_json([SYSTEM_IMAGE_URL]))
            .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
            .add_file("zbi", "fake zbi")),
    ]);

    env.resolver
        .url(SYSTEM_IMAGE_URL)
        .resolve(&env.resolver.package("system_image/0", SYSTEM_IMAGE_HASH));

    let () = env.run_update().await.expect("run system updater");

    assert_eq!(
        env.take_interactions(),
        vec![
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
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::B
            }),
            Paver(PaverEvent::BootManagerFlush),
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
            ClearRetainedPackages,
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
            ReplaceRetainedPackages(vec![SYSTEM_IMAGE_HASH.parse().unwrap(),]),
            Gc,
            PackageResolve(SYSTEM_IMAGE_URL.to_string()),
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

// Verifies that:
//   1. pinned update pkg url causes both RetainedPackages uses to include the update package
//   2. second RetainedPackages only has the update package
#[fasync::run_singlethreaded(test)]
async fn pinned_url_and_empty_packages_json() {
    let env = TestEnv::builder().build().await;

    env.resolver.url(UPDATE_PKG_URL_PINNED).respond_serially(vec![
        // To trigger first RetainedPackages use, first two resolves of update package must fail.
        Err(ResolveError::NoSpace),
        Err(ResolveError::NoSpace),
        Ok(env
            .resolver
            .package("update", UPDATE_HASH)
            .add_file("packages.json", make_packages_json([]))
            .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
            .add_file("zbi", "fake zbi")),
    ]);

    env.resolver
        .url(SYSTEM_IMAGE_URL)
        .resolve(&env.resolver.package("system_image/0", SYSTEM_IMAGE_HASH));

    let () = env
        .run_update_with_options(UPDATE_PKG_URL_PINNED, default_options())
        .await
        .expect("run system updater");

    assert_eq!(
        env.take_interactions(),
        vec![
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
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::B
            }),
            Paver(PaverEvent::BootManagerFlush),
            PackageResolve(UPDATE_PKG_URL_PINNED.to_string()),
            Gc,
            PackageResolve(UPDATE_PKG_URL_PINNED.to_string()),
            ReplaceRetainedPackages(vec![UPDATE_HASH.parse().unwrap()]),
            Gc,
            PackageResolve(UPDATE_PKG_URL_PINNED.to_string()),
            ReplaceRetainedPackages(vec![UPDATE_HASH.parse().unwrap(),]),
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

// Verifies that:
//   1. unpinned update pkg url causes first RetainedPackages use to be Clear and second use to
//      not include the update package
//   2. second RetainedPackages has no packages from packages.json so should be empty
#[fasync::run_singlethreaded(test)]
async fn unpinned_url_and_empty_packages_json() {
    let env = TestEnv::builder().build().await;

    env.resolver.url(UPDATE_PKG_URL).respond_serially(vec![
        // To trigger first RetainedPackages use, first two resolves of update package must fail.
        Err(ResolveError::NoSpace),
        Err(ResolveError::NoSpace),
        Ok(env
            .resolver
            .package("update", UPDATE_HASH)
            .add_file("packages.json", make_packages_json([]))
            .add_file("epoch.json", make_epoch_json(SOURCE_EPOCH))
            .add_file("zbi", "fake zbi")),
    ]);

    env.resolver
        .url(SYSTEM_IMAGE_URL)
        .resolve(&env.resolver.package("system_image/0", SYSTEM_IMAGE_HASH));

    let () = env.run_update().await.expect("run system updater");

    assert_eq!(
        env.take_interactions(),
        vec![
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
            Paver(PaverEvent::SetConfigurationUnbootable {
                configuration: paver::Configuration::B
            }),
            Paver(PaverEvent::BootManagerFlush),
            PackageResolve(UPDATE_PKG_URL.to_string()),
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
            ClearRetainedPackages,
            Gc,
            PackageResolve(UPDATE_PKG_URL.to_string()),
            ReplaceRetainedPackages(vec![]),
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
