// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::TestEnv,
    assert_matches::assert_matches,
    fidl_fuchsia_io as fio,
    fuchsia_pkg_testing::{Package, PackageBuilder, SystemImageBuilder},
    fuchsia_zircon::Status,
};

/// Test executability enforcement of fuchsia.pkg/PackageCache.{Get|Open}, i.e. whether the
/// handle to the package directory has RIGHT_EXECUTABLE.
///
/// If executability enforcement is enabled (the default), the handle should have RIGHT_EXECUTABLE
/// according to the following table (active means active in the dynamic index, other includes
/// being in the retained index):
///
/// | Location | Allowlisted | Is Executable |
/// +----------+-------------+---------------|
/// | base     | yes         | yes           |
/// | base     | no          | yes           |
/// | active   | yes         | yes           |
/// | active   | no          | no            |
/// | other    | yes         | no            |
/// | other    | no          | no            |
///
/// If executability enforcement is disabled (by the presence of file
/// data/pkgfs_disable_executability_restrictions in the meta.far of the system_image package
/// (just the meta.far, the blob can be missing from blobfs)) then the handle should always have
/// RIGHT_EXECUTABLE.

#[derive(Debug, Clone, Copy)]
enum IsRetained {
    True,
    False,
}

// Creates a blobfs containing `pkg` and `system_image`.
// Optionally adds `pkg` to the retained index.
// Does a Get and Open of `pkg` and compares the handle's flags to `expected_flags`.
async fn verify_package_executability(
    pkg: Package,
    system_image: SystemImageBuilder<'_>,
    is_retained: IsRetained,
    expected_flags: fio::OpenFlags,
) {
    let system_image = system_image.build().await;
    let env = TestEnv::builder()
        .blobfs_from_system_image_and_extra_packages(&system_image, &[&pkg])
        .build()
        .await;

    if let IsRetained::True = is_retained {
        let () = crate::replace_retained_packages(
            &env.proxies.retained_packages,
            &[(*pkg.meta_far_merkle_root()).into()],
        )
        .await;
    }

    async fn verify_flags(dir: &fio::DirectoryProxy, expected_flags: fio::OpenFlags) {
        let (status, flags) = dir.get_flags().await.unwrap();
        let () = Status::ok(status).unwrap();
        assert_eq!(flags, expected_flags);
    }

    // Verify Get flags
    let dir = crate::verify_package_cached(&env.proxies.package_cache, &pkg).await;
    let () = verify_flags(&dir, expected_flags).await;

    // Verify Open flags
    let open_res = env.open_package(&pkg.meta_far_merkle_root().to_string()).await;
    let () = match is_retained {
        IsRetained::True => assert_matches!(open_res, Err(status) if status == Status::NOT_FOUND),
        IsRetained::False => verify_flags(&open_res.unwrap(), expected_flags).await,
    };

    let () = env.stop().await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn base_package_executable() {
    let pkg = PackageBuilder::new("base-package").build().await.unwrap();
    let system_image = SystemImageBuilder::new().static_packages(&[&pkg]);

    let () = verify_package_executability(
        pkg,
        system_image,
        IsRetained::False,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
    )
    .await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn allowlisted_dynamic_index_active_package_executable() {
    let pkg = PackageBuilder::new("cache-package").build().await.unwrap();
    let system_image = SystemImageBuilder::new()
        .cache_packages(&[&pkg])
        .pkgfs_non_static_packages_allowlist(&["cache-package"]);

    let () = verify_package_executability(
        pkg,
        system_image,
        IsRetained::False,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
    )
    .await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn dynamic_index_active_package_not_executable() {
    let pkg = PackageBuilder::new("cache-package").build().await.unwrap();
    let system_image = SystemImageBuilder::new().cache_packages(&[&pkg]);

    let () = verify_package_executability(
        pkg,
        system_image,
        IsRetained::False,
        fio::OpenFlags::RIGHT_READABLE,
    )
    .await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn allowlisted_retained_index_package_not_executable() {
    let pkg = PackageBuilder::new("retained-package").build().await.unwrap();
    let system_image =
        SystemImageBuilder::new().pkgfs_non_static_packages_allowlist(&["retained-package"]);

    let () = verify_package_executability(
        pkg,
        system_image,
        IsRetained::True,
        fio::OpenFlags::RIGHT_READABLE,
    )
    .await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn retained_index_package_not_executable() {
    let pkg = PackageBuilder::new("retained-package").build().await.unwrap();
    let system_image = SystemImageBuilder::new();

    let () = verify_package_executability(
        pkg,
        system_image,
        IsRetained::True,
        fio::OpenFlags::RIGHT_READABLE,
    )
    .await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn enforcement_disabled_retained_index_package_executable() {
    let pkg = PackageBuilder::new("retained-package").build().await.unwrap();
    let system_image = SystemImageBuilder::new().pkgfs_disable_executability_restrictions();

    let () = verify_package_executability(
        pkg,
        system_image,
        IsRetained::True,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
    )
    .await;
}
