// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    crate::*,
    assert_matches::assert_matches,
    blobfs_ramdisk::BlobfsRamdisk,
    fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_merkle::Hash,
    fuchsia_pkg::{OpenRights, PackageDirectory},
    fuchsia_pkg_testing::{Package, PackageBuilder, SystemImageBuilder, VerificationError},
    fuchsia_zircon::Status,
    pkgfs_ramdisk::PkgfsRamdisk,
    std::{
        fmt::Debug,
        fs,
        io::{self, Read, Write},
    },
};

/// Helper function implementing the logic for the assert_error_kind! macro
///
/// Returns Ok(io:Error) if the kind matches, otherwise returns Err(String) with the panic message.
fn assert_error_kind_helper<T: Debug>(
    result: Result<T, io::Error>,
    result_expr: &'static str,
    expected: io::ErrorKind,
) -> Result<io::Error, String> {
    match result {
        Ok(val) => Err(format!(
            r"assertion failed: `{}.is_err()`
            result: `Ok({:?})`,
            expected: `Err({{ kind: {:?}, .. }})`",
            result_expr, val, expected
        )),
        Err(err) if err.kind() == expected => Ok(err),
        Err(err) => Err(format!(
            r"assertion failed: `{expr}.unwrap_err().kind() == {expected:?}`
            err.kind(): `{kind:?}`,
            expected: `{expected:?}`
            full err: `{full:?}`",
            expr = result_expr,
            expected = expected,
            kind = err.kind(),
            full = err,
        )),
    }
}

macro_rules! assert_error_kind {
    ($result:expr, $expected:expr) => {{
        assert_error_kind_helper($result, stringify!($result), $expected)
            .unwrap_or_else(|err_string| panic!("{}", err_string))
    }};
    ($result:expr, $expected:expr,) => {{
        assert_error_kind!($result, $expected)
    }};
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_short_write() {
    let pkgfs = PkgfsRamdisk::builder()
        .enforce_packages_non_static_allowlist(false) // turn off allowlist enforcement
        .start()
        .unwrap();

    let blobfs_root_dir = pkgfs.blobfs().root_dir().unwrap();
    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    let pkg = example_package().await;
    assert_eq!(
        pkg.meta_far_merkle_root(),
        &"dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
            .parse::<fuchsia_merkle::Hash>()
            .unwrap()
    );

    let mut meta_far = pkg.meta_far().expect("meta.far");
    {
        let mut to_write = d
            .new_file(
                "install/pkg/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b",
                0600,
            )
            .expect("create install file");
        to_write.set_len(meta_far.metadata().unwrap().len()).expect("truncate meta.far");
        io::copy(&mut meta_far, &mut to_write).expect("write meta.far");
    }
    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
            )
            .expect("list dir")
        )
        .expect("list dir contents"),
        ["e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"]
    );

    // Short blob write
    {
        let mut blob_install = d
            .new_file(
                "install/blob/e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3",
                0600,
            )
            .expect("create blob install file");
        let blob_contents = b"Hello world!\n";
        blob_install.set_len(blob_contents.len() as u64).expect("truncate blob");
        blob_install.write_all(b"Hello world!").expect("write blob");
    }

    // Blob still needed
    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
            )
            .expect("list dir")
        )
        .expect("list dir contents"),
        ["e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"]
    );

    // Full blob write
    {
        let mut blob_install = d
            .new_file(
                "install/blob/e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3",
                0600,
            )
            .expect("create blob install file");
        let blob_contents = b"Hello world!\n";
        blob_install.set_len(blob_contents.len() as u64).expect("truncate blob");
        blob_install.write_all(blob_contents).expect("write blob");
    }

    // Blob needs no more packages
    assert_error_kind!(
        d.list_dir(
            "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b",
        ),
        io::ErrorKind::NotFound,
    );

    verify_contents(&pkg, subdir_proxy(&d, "packages/example/0"))
        .await
        .expect("valid example package");
    let mut file_contents = String::new();
    d.open_file("versions/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b/a/b")
        .expect("read package file")
        .read_to_string(&mut file_contents)
        .expect("read package file");
    assert_eq!(&file_contents, "Hello world!\n");

    assert_eq!(
        ls_simple(blobfs_root_dir.list_dir(".").expect("list dir")).expect("list dir contents"),
        [
            "dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b",
            "e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"
        ],
    );

    drop(d);

    pkgfs.stop().await.expect("stopping pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_restart_install() {
    let pkgfs = PkgfsRamdisk::builder()
        .enforce_packages_non_static_allowlist(false) // turn off allowlist enforcement
        .start()
        .unwrap();

    let blobfs_root_dir = pkgfs.blobfs().root_dir().unwrap();
    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    let pkg = example_package().await;
    assert_eq!(
        pkg.meta_far_merkle_root(),
        &"dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
            .parse::<fuchsia_merkle::Hash>()
            .unwrap()
    );

    // Start package install
    // first, some checks to see if it's already installed
    assert_error_kind!(
        d.metadata("versions/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b")
            .map(|m| m.is_dir()),
        io::ErrorKind::NotFound,
    );
    assert_error_kind!(
        d.metadata(
            "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
        )
        .map(|m| m.is_dir()),
        io::ErrorKind::NotFound,
    );
    // Install the meta.far
    {
        let mut meta_far = pkg.meta_far().expect("meta.far");
        let mut to_write = d
            .new_file(
                "install/pkg/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b",
                0600,
            )
            .expect("create install file");
        to_write.set_len(meta_far.metadata().unwrap().len()).expect("truncate meta.far");
        io::copy(&mut meta_far, &mut to_write).expect("write meta.far");
    }
    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
            )
            .expect("list dir")
        )
        .expect("list dir contents"),
        ["e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"]
    );

    // Short blob write
    {
        let mut blob_install = d
            .new_file(
                "install/blob/e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3",
                0600,
            )
            .expect("create blob install file");
        let blob_contents = b"Hello world!\n";
        blob_install.set_len(blob_contents.len() as u64).expect("truncate blob");
        blob_install.write_all(b"Hello world!").expect("write blob");
    }

    // Blob still needed
    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
            )
            .expect("list dir")
        )
        .expect("list dir contents"),
        ["e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"]
    );

    // Restart pkgfs (without dynamic index)
    drop(d);
    let pkgfs = pkgfs.restart().expect("restarting pkgfs");
    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    // Restart package install
    assert_error_kind!(
        d.metadata("versions/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b")
            .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );
    assert_error_kind!(
        d.metadata(
            "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
        )
        .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );

    // Retry installing meta.far (fails with AlreadyExists)
    assert_error_kind!(
        d.new_file(
            "install/pkg/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b",
            0600,
        )
        .map(|_| ()),
        io::ErrorKind::AlreadyExists
    );

    assert_error_kind!(
        d.metadata("versions/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b")
            .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );
    // Check needs again.
    assert_eq!(
        d.metadata(
            "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
        )
        .map(|m| m.is_dir())
        .map_err(|e| format!("{:?}", e)),
        Ok(true)
    );

    // Needs exists, so we don't need to worry about meta.far write having failed.

    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
            )
            .expect("list dir")
        )
        .expect("list dir contents"),
        ["e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"]
    );

    // Full blob write
    {
        let mut blob_install = d
            .new_file(
                "install/blob/e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3",
                0600,
            )
            .expect("create blob install file");
        let blob_contents = b"Hello world!\n";
        blob_install.set_len(blob_contents.len() as u64).expect("truncate blob");
        blob_install.write_all(blob_contents).expect("write blob");
    }

    // Blob Needs no more packages
    assert_error_kind!(
        d.list_dir(
            "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
        ),
        io::ErrorKind::NotFound
    );

    verify_contents(&pkg, subdir_proxy(&d, "packages/example/0"))
        .await
        .expect("valid example package");
    let mut file_contents = String::new();
    d.open_file("versions/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b/a/b")
        .expect("read versions file")
        .read_to_string(&mut file_contents)
        .expect("read versions file");
    assert_eq!(&file_contents, "Hello world!\n");

    assert_eq!(
        ls_simple(blobfs_root_dir.list_dir(".").expect("list dir")).expect("list dir contents"),
        [
            "dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b",
            "e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"
        ],
    );

    drop(d);

    pkgfs.stop().await.expect("stopping pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_restart_install_already_done() {
    let pkgfs = PkgfsRamdisk::builder()
        .enforce_packages_non_static_allowlist(false) // turn off allowlist enforcement
        .start()
        .unwrap();

    let blobfs_root_dir = pkgfs.blobfs().root_dir().unwrap();
    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    let pkg = example_package().await;
    assert_eq!(
        pkg.meta_far_merkle_root(),
        &"dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
            .parse::<fuchsia_merkle::Hash>()
            .unwrap()
    );

    // Start package install
    // first, some checks to see if it's already installed
    assert_error_kind!(
        d.metadata("versions/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b")
            .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );
    assert_error_kind!(
        d.metadata(
            "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
        )
        .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );
    // Install the meta.far
    {
        let mut meta_far = pkg.meta_far().expect("meta.far");
        let mut to_write = d
            .new_file(
                "install/pkg/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b",
                0600,
            )
            .expect("create install file");
        to_write.set_len(meta_far.metadata().unwrap().len()).expect("truncate meta.far");
        io::copy(&mut meta_far, &mut to_write).expect("write meta.far");
    }
    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
            )
            .expect("list dir")
        )
        .expect("list dir contents"),
        ["e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"]
    );

    // blob write direct to blobfs
    {
        let mut blob_install = blobfs_root_dir
            .new_file("e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3", 0600)
            .expect("create blob install file");
        let blob_contents = b"Hello world!\n";
        blob_install.set_len(blob_contents.len() as u64).expect("truncate blob");
        blob_install.write_all(blob_contents).expect("write blob");
    }

    // Blob still needed
    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
            )
            .expect("list dir")
        )
        .expect("list dir contents"),
        ["e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"]
    );

    // Restart pkgfs (without dynamic index)
    drop(d);
    let pkgfs = pkgfs.restart().expect("restarting pkgfs");
    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    // Restart package install
    assert_error_kind!(
        d.metadata("versions/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b")
            .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );
    assert_error_kind!(
        d.metadata(
            "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
        )
        .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );

    // Retry installing meta.far (fails with Invalid argument)
    d.new_file(
        "install/pkg/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b",
        0600,
    )
    .expect_err("already exists");

    // Recheck versions
    assert_eq!(
        d.metadata("versions/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b")
            .map(|m| m.is_dir())
            .map_err(|e| format!("{:?}", e)),
        Ok(true)
    );

    // Check needs again.
    assert_error_kind!(
        d.metadata(
            "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
        )
        .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );

    verify_contents(&pkg, subdir_proxy(&d, "packages/example/0"))
        .await
        .expect("valid example package");
    let mut file_contents = String::new();
    d.open_file("versions/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b/a/b")
        .expect("read package file")
        .read_to_string(&mut file_contents)
        .expect("read package file");
    assert_eq!(&file_contents, "Hello world!\n");

    assert_eq!(
        ls_simple(blobfs_root_dir.list_dir(".").expect("list dir")).expect("list dir contents"),
        [
            "dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b",
            "e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"
        ],
    );

    drop(d);

    pkgfs.stop().await.expect("stopping pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_restart_install_failed_meta_far() {
    let pkgfs = PkgfsRamdisk::start().expect("starting pkgfs");
    let blobfs_root_dir = pkgfs.blobfs().root_dir().unwrap();
    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    let pkg = example_package().await;
    assert_eq!(
        pkg.meta_far_merkle_root(),
        &"dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
            .parse::<fuchsia_merkle::Hash>()
            .unwrap()
    );

    // Start package install
    // first, some checks to see if it's already installed
    assert_error_kind!(
        d.metadata("versions/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b")
            .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );
    assert_error_kind!(
        d.metadata(
            "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
        )
        .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );

    // Create (but don't write) the meta.far
    d.new_file(
        "install/pkg/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b",
        0600,
    )
    .expect("create install file");

    assert_error_kind!(
        d.metadata("versions/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b")
            .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );

    assert_eq!(
        ls_simple(d.list_dir("needs/packages").expect("list dir")).expect("list dir contents"),
        ["dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"]
    );

    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
            )
            .expect("list dir")
        )
        .expect("list dir contents"),
        Vec::<&str>::new()
    );

    assert_eq!(
        ls_simple(blobfs_root_dir.list_dir(".").expect("list dir")).expect("list dir contents"),
        Vec::<&str>::new()
    );

    drop(d);

    pkgfs.stop().await.expect("stopping pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_with_empty_static_index() {
    let system_image_package = SystemImageBuilder::new().build().await;

    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();
    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    verify_contents(&system_image_package, subdir_proxy(&d, "packages/system_image/0"))
        .await
        .expect("valid /packages/system_image/0");
    verify_contents(&system_image_package, subdir_proxy(&d, "system"))
        .await
        .expect("valid /system");
    verify_contents(
        &system_image_package,
        subdir_proxy(&d, &format!("versions/{}", system_image_package.meta_far_merkle_root())),
    )
    .await
    .expect("system_image in /versions");

    drop(d);

    pkgfs.stop().await.expect("stopping pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_with_system_image_meta_far_missing() {
    let blobfs = BlobfsRamdisk::start().unwrap();

    // Arbitrarily pick a system_image merkle (that isn't present)
    let system_image_merkle: Hash =
        "22e41860aa333dec2aea3899aa764a53a6ea7c179e6c47bf3a8163d89024343e".parse().unwrap();
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(&system_image_merkle)
        .start()
        .unwrap();
    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    assert_error_kind!(d.open_file("packages/system_image/0/meta"), io::ErrorKind::NotFound);
    assert_eq!(
        ls_simple(d.list_dir("system").unwrap()).unwrap_err().raw_os_error(),
        Some(libc::EOPNOTSUPP),
    );

    drop(d);

    pkgfs.stop().await.expect("stopping pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_with_system_image_base_package_missing() {
    let pkg = example_package().await;
    let system_image_package = SystemImageBuilder::new().static_packages(&[&pkg]).build().await;

    let blobfs = BlobfsRamdisk::start().unwrap();

    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());

    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();
    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    verify_contents(&system_image_package, subdir_proxy(&d, "system"))
        .await
        .expect("valid system_image");

    assert_eq!(sorted(ls(&pkgfs, "packages").unwrap()), ["example", "system_image"]);

    assert_error_kind!(d.list_dir("packages/example/0"), io::ErrorKind::NotFound);

    drop(d);

    pkgfs.stop().await.expect("stopping pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_with_system_image_base_package_missing_content_blob() {
    let pkg = example_package().await;
    let system_image_package = SystemImageBuilder::new().static_packages(&[&pkg]).build().await;

    let blobfs = BlobfsRamdisk::start().unwrap();

    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    blobfs.add_blob_from(&pkg.meta_far_merkle_root(), pkg.meta_far().unwrap()).unwrap();

    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();
    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    verify_contents(&system_image_package, subdir_proxy(&d, "system"))
        .await
        .expect("valid system_image");

    assert_eq!(sorted(ls(&pkgfs, "packages").unwrap()), ["example", "system_image"]);

    let mut file_contents = String::new();
    d.open_file("packages/example/0/meta")
        .expect("example should be present")
        .read_to_string(&mut file_contents)
        .expect("example meta should be readable");
    assert_eq!(file_contents.parse(), Ok(*pkg.meta_far_merkle_root()));

    // Can even list the package
    let contents = ls_simple(d.list_dir("packages/example/0/a").unwrap()).unwrap();
    assert_eq!(contents, &["b"]);

    // Can't read the file
    assert_matches!(
        verify_contents(&pkg, subdir_proxy(&d, "packages/example/0")).await,
        Err(VerificationError::MissingFile { path }) if path == "a/b"
    );

    drop(d);

    pkgfs.stop().await.expect("stopping pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_packages_doesnt_show_base_package_updates() {
    let original_base_pkg = PackageBuilder::new("example")
        .add_resource_at("a/b", "Hello world!\n".as_bytes())
        .build()
        .await
        .expect("build package");
    let system_image_package =
        SystemImageBuilder::new().static_packages(&[&original_base_pkg]).build().await;

    let blobfs = BlobfsRamdisk::start().unwrap();

    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    original_base_pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());

    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();
    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    // When we start, pkgfs/packages contains the original base pkg
    verify_contents(&original_base_pkg, subdir_proxy(&d, "packages/example/0"))
        .await
        .expect("valid system_image");
    let expected_versions = vec![
        original_base_pkg.meta_far_merkle_root().to_string(),
        system_image_package.meta_far_merkle_root().to_string(),
    ];
    assert_eq!(sorted(ls(&pkgfs, "versions").unwrap()), sorted(expected_versions));

    // Update the base package
    let updated_base_pkg = PackageBuilder::new("example")
        .add_resource_at("a/b", "UPDATED Hello world!\n".as_bytes())
        .build()
        .await
        .expect("build package");
    install(&pkgfs, &updated_base_pkg);

    // pkgfs/packages SHOULD NOT see the update, whereas pkgfs/versions SHOULD
    verify_contents(&original_base_pkg, subdir_proxy(&d, "packages/example/0"))
        .await
        .expect("valid system_image");
    let expected_versions: Vec<String> =
        vec![original_base_pkg, updated_base_pkg, system_image_package]
            .into_iter()
            .map(|pkg| pkg.meta_far_merkle_root().to_string())
            .collect();
    assert_eq!(sorted(ls(&pkgfs, "versions").unwrap()), sorted(expected_versions));

    // Drop the directory before we shutdown the server that's serving it.
    // In practice, this probably doesn't make a difference.
    drop(d);

    pkgfs.stop().await.expect("stopping pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_restart_deactivates_ephemeral_packages() {
    let pkgfs = PkgfsRamdisk::builder()
        .enforce_packages_non_static_allowlist(false) // turn off allowlist enforcement
        .start()
        .unwrap();

    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    let pkg = example_package().await;
    install(&pkgfs, &pkg);

    assert_eq!(ls(&pkgfs, "packages").unwrap(), ["example"]);
    assert_eq!(ls_simple(d.list_dir("packages/example").unwrap()).unwrap(), ["0"]);
    verify_contents(&pkg, subdir_proxy(&d, "packages/example/0"))
        .await
        .expect("valid example package");

    drop(d);
    let pkgfs = pkgfs.restart().unwrap();
    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    // package is no longer accesible.
    assert_eq!(ls(&pkgfs, "packages").unwrap(), Vec::<&str>::new());
    assert_eq!(ls(&pkgfs, "versions").unwrap(), Vec::<&str>::new());
    assert_error_kind!(
        d.metadata("packages/example/0").map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );
    assert_error_kind!(
        d.metadata(&format!("versions/{}", pkg.meta_far_merkle_root())).map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );

    drop(d);

    pkgfs.stop().await.expect("stopping pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_with_cache_index_missing_cache_meta_far() {
    let pkg = example_package().await;
    let system_image_package = SystemImageBuilder::new().cache_packages(&[&pkg]).build().await;

    let blobfs = BlobfsRamdisk::start().unwrap();

    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());

    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();
    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    verify_contents(&system_image_package, subdir_proxy(&d, "system"))
        .await
        .expect("valid system_image");

    assert_eq!(ls(&pkgfs, "packages").unwrap(), ["system_image"]);

    assert_error_kind!(d.open_file("packages/example/0/meta"), io::ErrorKind::NotFound);

    assert_error_kind!(
        d.open_file(format!("versions/{}/meta", pkg.meta_far_merkle_root())),
        io::ErrorKind::NotFound
    );

    drop(d);

    pkgfs.stop().await.expect("stopping pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_with_cache_index_missing_cache_content_blob() {
    let pkg = example_package().await;
    let system_image_package = SystemImageBuilder::new().cache_packages(&[&pkg]).build().await;

    let blobfs = BlobfsRamdisk::start().unwrap();

    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    blobfs.add_blob_from(&pkg.meta_far_merkle_root(), pkg.meta_far().unwrap()).unwrap();

    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();
    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    verify_contents(&system_image_package, subdir_proxy(&d, "system"))
        .await
        .expect("valid system_image");

    assert_eq!(ls(&pkgfs, "packages").unwrap(), ["system_image"]);

    assert_error_kind!(d.open_file("packages/example/0/meta"), io::ErrorKind::NotFound);

    assert_error_kind!(
        d.open_file(format!("versions/{}/meta", pkg.meta_far_merkle_root())),
        io::ErrorKind::NotFound,
    );

    assert_eq!(ls_simple(d.list_dir("needs/packages").unwrap()).unwrap(), Vec::<&str>::new());

    drop(d);

    pkgfs.stop().await.expect("stopping pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs() {
    let pkgfs = PkgfsRamdisk::builder()
        .enforce_packages_non_static_allowlist(false) // turn off allowlist enforcement
        .start()
        .unwrap();

    let blobfs_root_dir = pkgfs.blobfs().root_dir().unwrap();
    let d = pkgfs.root_dir().unwrap();

    let pkg = PackageBuilder::new("example")
        .add_resource_at("a/b", "Hello world!\n".as_bytes())
        .build()
        .await
        .expect("build package");

    // merkle root of pkg's meta.far.
    const META_FAR_MERKLE_ROOT: &'static str =
        "dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b";
    assert_eq!(
        pkg.meta_far_merkle_root(),
        &META_FAR_MERKLE_ROOT.parse::<fuchsia_merkle::Hash>().unwrap()
    );

    // merkle root of "Hello world!\n", the single blob in pkg.
    const CONTENT_BLOB_MERKLE_ROOT: &'static str =
        "e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3";
    assert_eq!(
        pkg.meta_contents().unwrap().contents()["a/b"],
        CONTENT_BLOB_MERKLE_ROOT.parse::<fuchsia_merkle::Hash>().unwrap()
    );

    let mut meta_far = pkg.meta_far().expect("meta.far");
    {
        let mut to_write = d
            .new_file(
                "install/pkg/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b",
                0600,
            )
            .expect("create install file");
        to_write.set_len(meta_far.metadata().unwrap().len()).expect("set_len meta.far");
        std::io::copy(&mut meta_far, &mut to_write).expect("write meta.far");
    }
    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
            )
            .expect("list dir")
        )
        .expect("list dir contents"),
        [CONTENT_BLOB_MERKLE_ROOT]
    );

    // Full blob write
    {
        let mut blob_install = d
            .new_file(
                "install/blob/e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3",
                0600,
            )
            .expect("create blob install file");
        let blob_contents = b"Hello world!\n";
        blob_install.set_len(blob_contents.len() as u64).expect("truncate blob");
        blob_install.write_all(blob_contents).expect("write blob");
    }

    // Blob Needs no more packages
    assert_eq!(
        d.list_dir(
            "needs/packages/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b"
        )
        .expect_err("check empty needs dir")
        .kind(),
        std::io::ErrorKind::NotFound
    );

    let mut file_contents = String::new();
    d.open_file("packages/example/0/a/b")
        .expect("read package file")
        .read_to_string(&mut file_contents)
        .expect("read package file");
    assert_eq!(&file_contents, "Hello world!\n");
    let mut file_contents = String::new();
    d.open_file("versions/dac936c184d9258e892591d6f060f4cbecbe6789b7d3bb993b1d1b26d32c344b/a/b")
        .expect("read package file")
        .read_to_string(&mut file_contents)
        .expect("read package file");
    assert_eq!(&file_contents, "Hello world!\n");

    assert_eq!(
        ls_simple(blobfs_root_dir.list_dir(".").expect("list dir")).expect("list dir contents"),
        [META_FAR_MERKLE_ROOT, CONTENT_BLOB_MERKLE_ROOT,],
    );

    drop(d);

    pkgfs.stop().await.unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_with_system_image() {
    let pkg = PackageBuilder::new("example")
        .add_resource_at("a/b", "Hello world!\n".as_bytes())
        .build()
        .await
        .expect("build package");

    let system_image_package = SystemImageBuilder::new().static_packages(&[&pkg]).build().await;

    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());

    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();
    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    let mut file_contents = String::new();
    d.open_file("packages/example/0/a/b")
        .expect("read package file1")
        .read_to_string(&mut file_contents)
        .expect("read package file2");
    assert_eq!(&file_contents, "Hello world!\n");

    let mut file_contents = String::new();
    d.open_file(format!("versions/{}/a/b", pkg.meta_far_merkle_root()))
        .expect("read package file3")
        .read_to_string(&mut file_contents)
        .expect("read package file4");
    assert_eq!(&file_contents, "Hello world!\n");

    drop(d);

    pkgfs.stop().await.expect("shutting down pkgfs");
}

async fn executable_package() -> Package {
    PackageBuilder::new("execute")
        .add_resource_at("meta/bin1", "please don't put binaries in meta fars".as_bytes())
        .add_resource_at("subdir/bin2", "or random subdirectories".as_bytes())
        .add_resource_at("bin/bin3", "/bin is good".as_bytes())
        .add_resource_at("lib/bin4", "/lib too".as_bytes())
        .build()
        .await
        .unwrap()
}

#[derive(Debug, Clone, Copy)]
enum Executability {
    AllowExecute,
    BlockExecute,
}

async fn verify_executable_package_rights_on_pkg_dir(
    pkgdir: PackageDirectory,
    executability: Executability,
) {
    // All resources should be openable without execute rights.
    assert_matches!(pkgdir.open_file("meta/bin1", OpenRights::Read).await, Ok(_));
    assert_matches!(pkgdir.open_file("subdir/bin2", OpenRights::Read).await, Ok(_));
    assert_matches!(pkgdir.open_file("bin/bin3", OpenRights::Read).await, Ok(_));
    assert_matches!(pkgdir.open_file("lib/bin4", OpenRights::Read).await, Ok(_));

    // Metadata is never allowed to be executable.
    assert_matches!(
        pkgdir.open_file("meta/bin1", OpenRights::ReadExecute).await,
        Err(pkgfs::OpenError::OpenError(_))
    );

    match executability {
        Executability::AllowExecute => {
            assert_matches!(pkgdir.open_file("subdir/bin2", OpenRights::ReadExecute).await, Ok(_));
            assert_matches!(pkgdir.open_file("bin/bin3", OpenRights::ReadExecute).await, Ok(_));
            assert_matches!(pkgdir.open_file("lib/bin4", OpenRights::ReadExecute).await, Ok(_));
        }

        Executability::BlockExecute => {
            // See //src/lib/thinfs/zircon/rpc/rpc.go's errorToZx for why fs.ErrPermission maps
            // to Status::BAD_HANDLE instead of Status::ACCESS_DENIED.
            assert_matches!(
                pkgdir.open_file("subdir/bin2", OpenRights::ReadExecute).await,
                Err(pkgfs::OpenError::OpenError(Status::BAD_HANDLE))
            );
            assert_matches!(
                pkgdir.open_file("bin/bin3", OpenRights::ReadExecute).await,
                Err(pkgfs::OpenError::OpenError(Status::BAD_HANDLE))
            );
            assert_matches!(
                pkgdir.open_file("lib/bin4", OpenRights::ReadExecute).await,
                Err(pkgfs::OpenError::OpenError(Status::BAD_HANDLE))
            );
        }
    }
}

async fn verify_executable_package_rights(
    pkgfs: &PkgfsRamdisk,
    pkg: &Package,
    executability: Executability,
) {
    let root_dir_proxy = pkgfs.root_dir_proxy().unwrap();
    let pkg_merkle = pkg.meta_far_merkle_root();

    // Peform the same checks against the package opened through /pkgfs/packages and
    // /pkgfs/versions.

    let pkgfs_packages = pkgfs::packages::Client::open_from_pkgfs_root(&root_dir_proxy).unwrap();
    let pkgdir = pkgfs_packages.open_package("execute", None).await.unwrap();
    verify_executable_package_rights_on_pkg_dir(pkgdir, executability).await;

    let pkgfs_versions = pkgfs::versions::Client::open_from_pkgfs_root(&root_dir_proxy).unwrap();
    let pkgdir = pkgfs_versions.open_package(pkg_merkle).await.unwrap();
    verify_executable_package_rights_on_pkg_dir(pkgdir, executability).await;
}

#[fasync::run_singlethreaded(test)]
async fn executability_enforcement_allows_base_package() {
    let pkg = executable_package().await;
    let system_image_package = SystemImageBuilder::new()
        .static_packages(&[&pkg])
        .pkgfs_non_static_packages_allowlist(&["execute"])
        .build()
        .await;

    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .enforce_non_base_executability_restrictions(true)
        .start()
        .unwrap();

    verify_executable_package_rights(&pkgfs, &pkg, Executability::AllowExecute).await;

    pkgfs.stop().await.expect("shutting down pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn executability_enforcement_blocks_unknown_package() {
    let pkg = executable_package().await;
    let system_image_package = SystemImageBuilder::new().build().await;

    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .enforce_non_base_executability_restrictions(true)
        .enforce_packages_non_static_allowlist(false)
        .start()
        .unwrap();

    install(&pkgfs, &pkg);

    verify_executable_package_rights(&pkgfs, &pkg, Executability::BlockExecute).await;

    pkgfs.stop().await.expect("shutting down pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn executability_enforcement_allows_unknown_package_if_dynamically_disabled() {
    let pkg = executable_package().await;
    let system_image_package = SystemImageBuilder::new().build().await;

    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .enforce_non_base_executability_restrictions(false)
        .enforce_packages_non_static_allowlist(false)
        .start()
        .unwrap();

    install(&pkgfs, &pkg);

    verify_executable_package_rights(&pkgfs, &pkg, Executability::AllowExecute).await;

    pkgfs.stop().await.expect("shutting down pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn executability_enforcement_blocks_update_to_base_package() {
    let pkg1 = PackageBuilder::new("execute")
        .add_resource_at("bin/app", "run me".as_bytes())
        .build()
        .await
        .unwrap();
    let pkg2 = executable_package().await;
    let system_image_package = SystemImageBuilder::new().static_packages(&[&pkg1]).build().await;

    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    pkg1.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .enforce_non_base_executability_restrictions(true)
        .start()
        .unwrap();
    let pkgfs_versions =
        pkgfs::versions::Client::open_from_pkgfs_root(&pkgfs.root_dir_proxy().unwrap()).unwrap();

    // Install a different package with the same name so the static index is updated.
    install(&pkgfs, &pkg2);

    // The base package is executable.
    let pkgdir1 = pkgfs_versions.open_package(pkg1.meta_far_merkle_root()).await.unwrap();
    assert_matches!(pkgdir1.open_file("bin/app", OpenRights::ReadExecute).await, Ok(_));

    // But the update to it isn't.
    let pkgdir2 = pkgfs_versions.open_package(pkg2.meta_far_merkle_root()).await.unwrap();
    verify_executable_package_rights_on_pkg_dir(pkgdir2, Executability::BlockExecute).await;

    pkgfs.stop().await.expect("shutting down pkgfs");
}

// A previous version of pkgfs/thinfs crashed on a call to get_flags on the /pkg directory, since get_flags
// was an unimplemented transitional method.
// TODO(fxbug.dev/55663): remove this test when unimplemented transitional methods do not crash the server.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_pkgfs_get_flags() {
    // Try get_flags on our own package directory.
    // thinfs returns an error for this call, which closes the channel.
    let this_pkg_dir =
        fuchsia_fs::open_directory_in_namespace("/pkg", fuchsia_fs::OpenFlags::RIGHT_READABLE)
            .expect("opening /pkg");

    let (status, flags) = this_pkg_dir.get_flags().await.expect("getting directory flags");
    assert_eq!(status, Status::OK.into_raw());
    assert_eq!(flags, fuchsia_fs::OpenFlags::RIGHT_READABLE);

    // Try get_flags on a file within our package directory.
    // thinfs maps GetFlags to GetFlags, so this should not close the channel.
    let meta_far_file_proxy =
        fuchsia_fs::open_file_in_namespace("/pkg/meta", fuchsia_fs::OpenFlags::RIGHT_READABLE)
            .expect("opening /pkg/meta as file");
    let (zx_result, flags) = meta_far_file_proxy.get_flags().await.expect("getting file flags");
    assert_eq!(zx_result, Status::OK.into_raw());
    assert_eq!(flags, fuchsia_fs::OpenFlags::RIGHT_READABLE);

    // We should still be able to read our own package directory and read our own merkle root,
    // which means pkgfs hasn't crashed.
    assert!(fs::read_to_string("/pkg/meta").expect("read to string").len() > 0);
}

// A previous version of pkgfs/thinfs crashed on a call to set_flags, since set_flags
// was an unimplemented transitional method.
// TODO(fxbug.dev/55663): remove this test when unimplemented transitional methods do not crash the server.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_pkgfs_set_flags() {
    // Try set_flags on our own package directory.
    // thinfs returns an error for this call, which closes the channel.
    let this_pkg_dir =
        fuchsia_fs::open_directory_in_namespace("/pkg", fuchsia_fs::OpenFlags::RIGHT_READABLE)
            .expect("opening /pkg");
    let status =
        this_pkg_dir.set_flags(fio::OpenFlags::APPEND).await.expect("setting directory flags");
    assert_eq!(status, Status::NOT_SUPPORTED.into_raw());

    // Try set_flags on a file within our package directory.
    // thinfs returns an error for this call, which closes the channel
    let meta_far_file_proxy =
        fuchsia_fs::open_file_in_namespace("/pkg/meta", fuchsia_fs::OpenFlags::RIGHT_READABLE)
            .expect("opening /pkg/meta as file");
    let status =
        meta_far_file_proxy.set_flags(fio::OpenFlags::APPEND).await.expect("setting file flags");
    assert_eq!(status, Status::OK.into_raw());

    // We should still be able to read our own package directory and read our own merkle root,
    // which means pkgfs hasn't crashed.
    assert!(fs::read_to_string("/pkg/meta").expect("read to string").len() > 0);
}

// Test that pkgfs correctly handles interleaved opens and closes on the same file in the meta
// virtual directory
#[fuchsia_async::run_singlethreaded(test)]
async fn test_multiple_opens_on_meta_file() {
    let package = PackageBuilder::new("example")
        .add_resource_at("meta/a", "Hello world!\n".as_bytes())
        .add_resource_at("meta/b", "These are some bytes\n".as_bytes())
        .add_resource_at("some/path", "An actual file\n".as_bytes())
        .build()
        .await
        .expect("build package");

    let system_image_package = SystemImageBuilder::new().static_packages(&[&package]).build().await;

    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();

    let d = pkgfs.root_dir_proxy().expect("getting pkgfs root dir");
    let meta_directory = fuchsia_fs::directory::open_directory(
        &d,
        &format!("versions/{}/meta/", package.meta_far_merkle_root()),
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
    )
    .await
    .expect("open meta dir");

    // We should be able to open a file twice, close one version, then read from the second.
    let file_a = fuchsia_fs::directory::open_file(
        &meta_directory,
        "a",
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
    )
    .await
    .unwrap();
    let file_a_2 = fuchsia_fs::directory::open_file(
        &meta_directory,
        "a",
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
    )
    .await
    .unwrap();

    file_a.close().await.unwrap().map_err(Status::from_raw).unwrap();
    let vmo = file_a_2
        .get_backing_memory(fio::VmoFlags::READ | fio::VmoFlags::PRIVATE_CLONE)
        .await
        .unwrap()
        .map_err(Status::from_raw)
        .unwrap();
    let size = vmo.get_content_size().unwrap();
    assert_ne!(size, 0);
    file_a_2.close().await.unwrap().map_err(Status::from_raw).unwrap();

    pkgfs.stop().await.expect("shutting down pkgfs");
}

// Test that pkgfs correctly handles closing a parent directory when a child file is open
#[fuchsia_async::run_singlethreaded(test)]
async fn test_opening_file_within_directory_and_closing_directory() {
    let package = PackageBuilder::new("example")
        .add_resource_at("meta/subdir/a", "Hello world!\n".as_bytes())
        .add_resource_at("meta/subdir/b", "These are some bytes\n".as_bytes())
        .build()
        .await
        .expect("build package");

    let system_image_package = SystemImageBuilder::new().static_packages(&[&package]).build().await;

    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();

    let d = pkgfs.root_dir_proxy().expect("getting pkgfs root dir");
    let meta_directory = fuchsia_fs::directory::open_directory(
        &d,
        &format!("versions/{}/meta/", package.meta_far_merkle_root()),
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
    )
    .await
    .expect("open meta dir");

    // We should be able to open subdir in a meta directory, open a file within it,
    // close the directory, and still read from the file.
    let subdir = fuchsia_fs::directory::open_directory(
        &meta_directory,
        "subdir",
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
    )
    .await
    .unwrap();
    let file_a =
        fuchsia_fs::directory::open_file(&subdir, "a", fuchsia_fs::OpenFlags::RIGHT_READABLE)
            .await
            .unwrap();
    subdir.close().await.unwrap().map_err(Status::from_raw).unwrap();
    let a_contents = fuchsia_fs::file::read_to_string(&file_a).await.unwrap();
    assert_eq!(a_contents, "Hello world!\n");
    file_a.close().await.unwrap().map_err(Status::from_raw).unwrap();

    pkgfs.stop().await.expect("shutting down pkgfs");
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_walking_the_pkg_dir() {
    let paths = [
        "meta/a",
        "meta/b",
        "meta/c/d/e/f/g",
        "meta/c/d/e/f/h",
        "meta/c/i",
        "meta/c/j",
        "a",
        "b/d/e/f/g",
        "b/d/e/f/h",
        "c/i",
        "c/j",
    ];

    let mut package = PackageBuilder::new("example");

    for path in &paths {
        package = package.add_resource_at(path, path.as_bytes());
    }

    let package = package.build().await.expect("build package");

    let system_image_package = SystemImageBuilder::new().static_packages(&[&package]).build().await;

    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();

    let pkgfs_dir = pkgfs.root_dir_proxy().expect("getting pkgfs root dir");

    // Make sure we can access the files directly through the root.
    {
        let pkg_directory = fuchsia_fs::directory::open_directory(
            &pkgfs_dir,
            &format!("versions/{}", package.meta_far_merkle_root()),
            fuchsia_fs::OpenFlags::RIGHT_READABLE,
        )
        .await
        .expect("open meta dir");

        for path in &paths {
            let file = fuchsia_fs::directory::open_file(
                &pkg_directory,
                path,
                fuchsia_fs::OpenFlags::RIGHT_READABLE,
            )
            .await
            .unwrap();

            let body = fuchsia_fs::file::read_to_string(&file).await.unwrap();
            assert_eq!(path, &body);
        }
    }

    // Next, walk through the directories to get to the files.
    for path in &paths {
        let mut d = fuchsia_fs::directory::open_directory(
            &pkgfs_dir,
            &format!("versions/{}", package.meta_far_merkle_root()),
            fuchsia_fs::OpenFlags::RIGHT_READABLE,
        )
        .await
        .expect("open meta dir");

        let mut entries = path.split('/');
        let mut entry = entries.next().unwrap();

        while let Some(child) = entries.next() {
            d = fuchsia_fs::directory::open_directory(
                &d,
                entry,
                fuchsia_fs::OpenFlags::RIGHT_READABLE,
            )
            .await
            .unwrap();

            entry = child;
        }

        let file =
            fuchsia_fs::directory::open_file(&d, entry, fuchsia_fs::OpenFlags::RIGHT_READABLE)
                .await
                .unwrap();
        let body = fuchsia_fs::file::read_to_string(&file).await.unwrap();
        assert_eq!(path, &body);
    }

    pkgfs.stop().await.expect("shutting down pkgfs");
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_interacting_with_broken_pkg_dir_does_not_break_pkgfs() {
    let expected_contents = "Hello world!\n";

    let package_to_corrupt = PackageBuilder::new("example0")
        .add_resource_at("meta/a", expected_contents.as_bytes())
        .build()
        .await
        .expect("build package");

    let package_still_good = PackageBuilder::new("example1")
        .add_resource_at("meta/b", expected_contents.as_bytes())
        .build()
        .await
        .expect("build package");

    let system_image_package = SystemImageBuilder::new()
        .static_packages(&[&package_to_corrupt, &package_still_good])
        .build()
        .await;

    let blobfs = BlobfsRamdisk::start().unwrap();
    let blobfs_proxy = blobfs.root_dir_proxy().unwrap();

    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    package_to_corrupt.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    package_still_good.write_to_blobfs_dir(&blobfs.root_dir().unwrap());

    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap();

    let pkgfs_dir = pkgfs.root_dir_proxy().expect("getting pkgfs root dir");

    // Open a pkg dir
    let pkg_dir = fuchsia_fs::directory::open_directory(
        &pkgfs_dir,
        &format!("versions/{}", package_to_corrupt.meta_far_merkle_root()),
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
    )
    .await
    .expect("open meta dir");

    // Delete the meta.far blob of the open pkg dir
    blobfs_proxy
        .unlink(&package_to_corrupt.meta_far_merkle_root().to_string(), fio::UnlinkOptions::EMPTY)
        .await
        .expect("unlink fidl")
        .expect("unlink status");

    // Opening the meta/ directory should fail
    assert_matches!(
        fuchsia_fs::directory::open_directory(
            &pkg_dir,
            "meta",
            fuchsia_fs::OpenFlags::RIGHT_READABLE
        ).await,
        Err(fuchsia_fs::node::OpenError::OpenError(s)) if s == Status::NOT_FOUND
    );

    // Opening a meta file should fail
    assert_matches!(
        fuchsia_fs::directory::open_file(
            &pkg_dir,
            "meta/a",
            fuchsia_fs::OpenFlags::RIGHT_READABLE
        ).await,
        Err(fuchsia_fs::node::OpenError::OpenError(s)) if s == Status::NOT_FOUND
    );

    // Interacting with other pkg dirs should continue to work
    let file = fuchsia_fs::directory::open_file(
        &pkgfs_dir,
        &format!("versions/{}/meta/b", package_still_good.meta_far_merkle_root()),
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
    )
    .await
    .unwrap();

    let actual_contents = fuchsia_fs::file::read_to_string(&file).await.unwrap();
    assert_eq!(actual_contents, expected_contents);

    pkgfs.stop().await.expect("shutting down pkgfs");
}
