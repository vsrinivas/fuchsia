// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    crate::*,
    blobfs_ramdisk::BlobfsRamdisk,
    fuchsia_async as fasync,
    fuchsia_pkg_testing::{PackageBuilder, SystemImageBuilder},
    pkgfs_ramdisk::PkgfsRamdisk,
    std::io,
};

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_install_update_after_gc() {
    // GC doesn't work without a working system image
    let system_image_package =
        SystemImageBuilder::new().pkgfs_non_static_packages_allowlist(&["example"]).build().await;

    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();

    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    let pkg = example_package().await;
    install(&pkgfs, &pkg);

    assert_eq!(ls_simple(d.list_dir("packages/example").unwrap()).unwrap(), ["0"]);
    verify_contents(&pkg, subdir_proxy(&d, "packages/example/0"))
        .await
        .expect("valid example package");

    let pkg2 = PackageBuilder::new("example")
        .add_resource_at("a/b", "Hello world 2!\n".as_bytes())
        .build()
        .await
        .expect("build package");
    install(&pkgfs, &pkg2);

    assert_eq!(sorted(ls(&pkgfs, "packages").unwrap()), ["example", "system_image"]);
    assert_eq!(ls_simple(d.list_dir("packages/example").unwrap()).unwrap(), ["0"]);
    verify_contents(&pkg2, subdir_proxy(&d, "packages/example/0"))
        .await
        .expect("pkg2 replaced pkg");

    assert_eq!(
        sorted(ls(&pkgfs, "versions").unwrap()),
        sorted(vec![
            pkg2.meta_far_merkle_root().to_string(),
            system_image_package.meta_far_merkle_root().to_string()
        ])
    );

    // old version is no longer accesible.
    assert_error_kind!(
        d.metadata(&format!("versions/{}", pkg.meta_far_merkle_root())).map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );

    {
        let blobfs_dir = pkgfs.blobfs().root_dir().unwrap();

        // Old blobs still in blobfs.
        let expected_blobs = sorted(
            pkg.list_blobs()
                .unwrap()
                .into_iter()
                .chain(pkg2.list_blobs().unwrap())
                .chain(system_image_package.list_blobs().unwrap())
                .map(|m| m.to_string())
                .collect(),
        );
        assert_eq!(sorted(ls_simple(blobfs_dir.list_dir(".").unwrap()).unwrap()), expected_blobs);

        // Trigger GC
        d.remove_dir("ctl/do-not-use-this-garbage").unwrap();

        // pkg blobs are in blobfs no longer
        let expected_blobs = sorted(
            pkg2.list_blobs()
                .unwrap()
                .into_iter()
                .chain(system_image_package.list_blobs().unwrap())
                .map(|m| m.to_string())
                .collect(),
        );
        let got_blobs = sorted(ls_simple(blobfs_dir.list_dir(".").unwrap()).unwrap());

        assert_eq!(got_blobs, expected_blobs);
    }

    drop(d);

    pkgfs.stop().await.expect("stopping pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_shadowed_cache_package() {
    let pkg = example_package().await;
    let system_image_package = SystemImageBuilder::new()
        .cache_packages(&[&pkg])
        .pkgfs_non_static_packages_allowlist(&["example"])
        .build()
        .await;

    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();

    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    assert_eq!(ls_simple(d.list_dir("packages/example").unwrap()).unwrap(), ["0"]);
    verify_contents(&pkg, subdir_proxy(&d, "packages/example/0"))
        .await
        .expect("valid example package");

    let pkg2 = PackageBuilder::new("example")
        .add_resource_at("a/b", "Hello world 2!\n".as_bytes())
        .build()
        .await
        .expect("build package");
    install(&pkgfs, &pkg2);

    assert_eq!(sorted(ls(&pkgfs, "packages").unwrap()), ["example", "system_image"]);
    assert_eq!(ls_simple(d.list_dir("packages/example").unwrap()).unwrap(), ["0"]);
    verify_contents(&pkg2, subdir_proxy(&d, "packages/example/0"))
        .await
        .expect("pkg2 replaced pkg");

    assert_eq!(
        sorted(ls(&pkgfs, "versions").unwrap()),
        sorted(vec![
            pkg2.meta_far_merkle_root().to_string(),
            system_image_package.meta_far_merkle_root().to_string()
        ])
    );

    // cached version is no longer accesible.
    assert_error_kind!(
        d.metadata(&format!("versions/{}", pkg.meta_far_merkle_root())).map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );

    {
        let blobfs_dir = pkgfs.blobfs().root_dir().unwrap();

        // Old blobs still in blobfs.
        let expected_blobs = sorted(
            pkg.list_blobs()
                .unwrap()
                .into_iter()
                .chain(pkg2.list_blobs().unwrap())
                .chain(system_image_package.list_blobs().unwrap())
                .map(|m| m.to_string())
                .collect(),
        );
        assert_eq!(sorted(ls_simple(blobfs_dir.list_dir(".").unwrap()).unwrap()), expected_blobs);

        // Trigger GC
        d.remove_dir("ctl/do-not-use-this-garbage").unwrap();

        // cached pkg blobs are in blobfs no longer
        let expected_blobs = sorted(
            pkg2.list_blobs()
                .unwrap()
                .into_iter()
                .chain(system_image_package.list_blobs().unwrap())
                .map(|m| m.to_string())
                .collect(),
        );
        let got_blobs = sorted(ls_simple(blobfs_dir.list_dir(".").unwrap()).unwrap());

        assert_eq!(got_blobs, expected_blobs);
    }

    drop(d);

    pkgfs.stop().await.expect("stopping pkgfs");
}
