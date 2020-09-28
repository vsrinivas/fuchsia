// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    blobfs_ramdisk::BlobfsRamdisk,
    fuchsia_async as fasync,
    fuchsia_merkle::Hash,
    fuchsia_pkg_testing::{Package, PackageBuilder, SystemImageBuilder, VerificationError},
    fuchsia_zircon::Status,
    matches::assert_matches,
    pkgfs::package::OpenRights,
    pkgfs_ramdisk::PkgfsRamdisk,
    std::{
        collections::HashSet,
        fmt::Debug,
        fs,
        future::Future,
        io::{self, Read, Write},
    },
};

mod get_buffer;

fn ls_simple(d: openat::DirIter) -> Result<Vec<String>, io::Error> {
    Ok(d.map(|i| i.map(|entry| entry.file_name().to_string_lossy().into()))
        .collect::<Result<Vec<_>, _>>()?)
}

trait AsRootDir {
    fn as_root_dir(&self) -> openat::Dir;
}

impl AsRootDir for PkgfsRamdisk {
    fn as_root_dir(&self) -> openat::Dir {
        self.root_dir().expect("getting pkgfs root dir")
    }
}

fn ls(root: &dyn AsRootDir, path: impl openat::AsPath) -> Result<Vec<String>, io::Error> {
    let d = root.as_root_dir();
    ls_simple(d.list_dir(path)?)
}

/// Allows us to call sort inline
fn sorted<T: Ord>(mut vec: Vec<T>) -> Vec<T> {
    vec.sort();
    vec
}

fn verify_contents<'a>(
    pkg: &'a Package,
    dir: fidl_fuchsia_io::DirectoryProxy,
) -> impl Future<Output = Result<(), VerificationError>> + 'a {
    async move { pkg.verify_contents(&dir).await }
}

fn subdir_proxy(d: &openat::Dir, path: &str) -> fidl_fuchsia_io::DirectoryProxy {
    let handle = fdio::transfer_fd(
        d.open_file(path).unwrap_or_else(|e| panic!("opening {}: {:?}", path, e)),
    )
    .unwrap();
    fidl_fuchsia_io::DirectoryProxy::new(
        fuchsia_async::Channel::from_channel(handle.into()).unwrap(),
    )
}

fn copy_file_with_len(
    d: &openat::Dir,
    path: &std::path::Path,
    source: &mut std::fs::File,
) -> Result<(), anyhow::Error> {
    use std::convert::TryInto;
    let mut bytes = vec![];
    source.read_to_end(&mut bytes)?;
    let mut file = d.write_file(path, 0777)?;
    file.set_len(bytes.len().try_into().unwrap())?;
    file.write_all(&bytes)?;
    Ok(())
}

fn install(pkgfs: &PkgfsRamdisk, pkg: &Package) {
    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    copy_file_with_len(
        &d,
        &std::path::Path::new(&format!("install/pkg/{}", pkg.meta_far_merkle_root())),
        &mut pkg.meta_far().unwrap(),
    )
    .unwrap();

    let mut needs: HashSet<String> =
        ls_simple(d.list_dir(format!("needs/packages/{}", pkg.meta_far_merkle_root())).unwrap())
            .unwrap()
            .into_iter()
            .collect();

    for mut content in pkg.content_blob_files() {
        let merkle = content.merkle.to_string();
        if needs.contains(&merkle) {
            copy_file_with_len(
                &d,
                &std::path::Path::new("install/blob").join(&merkle),
                &mut content.file,
            )
            .unwrap_or_else(|e| panic!("error writing {}: {:?}", merkle, e));
            needs.remove(&merkle);
        }
    }

    // Quick sanity check that we actually installed the package.
    let mut file_contents = String::new();
    d.open_file(format!("versions/{}/meta", pkg.meta_far_merkle_root()))
        .unwrap()
        .read_to_string(&mut file_contents)
        .unwrap();

    assert_eq!(file_contents, format!("{}", pkg.meta_far_merkle_root()))
}

/// Helper function implementing the logic for the asser_error_kind! macro
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
            .unwrap_or_else(|err_string| panic!(err_string))
    }};
    ($result:expr, $expected:expr,) => {{
        assert_error_kind!($result, $expected)
    }};
}

async fn example_package() -> Package {
    PackageBuilder::new("example")
        .add_resource_at("a/b", "Hello world!\n".as_bytes())
        .build()
        .await
        .expect("build package")
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
        &"b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
            .parse::<fuchsia_merkle::Hash>()
            .unwrap()
    );

    let mut meta_far = pkg.meta_far().expect("meta.far");
    {
        let mut to_write = d
            .new_file(
                "install/pkg/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
                0600,
            )
            .expect("create install file");
        to_write.set_len(meta_far.metadata().unwrap().len()).expect("truncate meta.far");
        io::copy(&mut meta_far, &mut to_write).expect("write meta.far");
    }
    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
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
                "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
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
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
        ),
        io::ErrorKind::NotFound,
    );

    verify_contents(&pkg, subdir_proxy(&d, "packages/example/0"))
        .await
        .expect("valid example package");
    let mut file_contents = String::new();
    d.open_file("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93/a/b")
        .expect("read package file")
        .read_to_string(&mut file_contents)
        .expect("read package file");
    assert_eq!(&file_contents, "Hello world!\n");

    assert_eq!(
        ls_simple(blobfs_root_dir.list_dir(".").expect("list dir")).expect("list dir contents"),
        [
            "b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
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
        &"b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
            .parse::<fuchsia_merkle::Hash>()
            .unwrap()
    );

    // Start package install
    // first, some checks to see if it's already installed
    assert_error_kind!(
        d.metadata("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93")
            .map(|m| m.is_dir()),
        io::ErrorKind::NotFound,
    );
    assert_error_kind!(
        d.metadata(
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
        )
        .map(|m| m.is_dir()),
        io::ErrorKind::NotFound,
    );
    // Install the meta.far
    {
        let mut meta_far = pkg.meta_far().expect("meta.far");
        let mut to_write = d
            .new_file(
                "install/pkg/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
                0600,
            )
            .expect("create install file");
        to_write.set_len(meta_far.metadata().unwrap().len()).expect("truncate meta.far");
        io::copy(&mut meta_far, &mut to_write).expect("write meta.far");
    }
    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
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
                "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
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
        d.metadata("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93")
            .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );
    assert_error_kind!(
        d.metadata(
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
        )
        .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );

    // Retry installing meta.far (fails with EEXIST)
    assert_error_kind!(
        d.new_file(
            "install/pkg/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
            0600,
        )
        .map(|_| ()),
        io::ErrorKind::AlreadyExists
    );

    assert_error_kind!(
        d.metadata("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93")
            .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );
    // Check needs again.
    assert_eq!(
        d.metadata(
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
        )
        .map(|m| m.is_dir())
        .map_err(|e| format!("{:?}", e)),
        Ok(true)
    );

    // Needs exists, so we don't need to worry about meta.far write having failed.

    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
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
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
        ),
        io::ErrorKind::NotFound
    );

    verify_contents(&pkg, subdir_proxy(&d, "packages/example/0"))
        .await
        .expect("valid example package");
    let mut file_contents = String::new();
    d.open_file("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93/a/b")
        .expect("read versions file")
        .read_to_string(&mut file_contents)
        .expect("read versions file");
    assert_eq!(&file_contents, "Hello world!\n");

    assert_eq!(
        ls_simple(blobfs_root_dir.list_dir(".").expect("list dir")).expect("list dir contents"),
        [
            "b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
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
        &"b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
            .parse::<fuchsia_merkle::Hash>()
            .unwrap()
    );

    // Start package install
    // first, some checks to see if it's already installed
    assert_error_kind!(
        d.metadata("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93")
            .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );
    assert_error_kind!(
        d.metadata(
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
        )
        .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );
    // Install the meta.far
    {
        let mut meta_far = pkg.meta_far().expect("meta.far");
        let mut to_write = d
            .new_file(
                "install/pkg/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
                0600,
            )
            .expect("create install file");
        to_write.set_len(meta_far.metadata().unwrap().len()).expect("truncate meta.far");
        io::copy(&mut meta_far, &mut to_write).expect("write meta.far");
    }
    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
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
                "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
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
        d.metadata("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93")
            .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );
    assert_error_kind!(
        d.metadata(
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
        )
        .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );

    // Retry installing meta.far (fails with Invalid argument)
    d.new_file(
        "install/pkg/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
        0600,
    )
    .expect_err("already exists");

    // Recheck versions
    assert_eq!(
        d.metadata("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93")
            .map(|m| m.is_dir())
            .map_err(|e| format!("{:?}", e)),
        Ok(true)
    );

    // Check needs again.
    assert_error_kind!(
        d.metadata(
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
        )
        .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );

    verify_contents(&pkg, subdir_proxy(&d, "packages/example/0"))
        .await
        .expect("valid example package");
    let mut file_contents = String::new();
    d.open_file("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93/a/b")
        .expect("read package file")
        .read_to_string(&mut file_contents)
        .expect("read package file");
    assert_eq!(&file_contents, "Hello world!\n");

    assert_eq!(
        ls_simple(blobfs_root_dir.list_dir(".").expect("list dir")).expect("list dir contents"),
        [
            "b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
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
        &"b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
            .parse::<fuchsia_merkle::Hash>()
            .unwrap()
    );

    // Start package install
    // first, some checks to see if it's already installed
    assert_error_kind!(
        d.metadata("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93")
            .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );
    assert_error_kind!(
        d.metadata(
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
        )
        .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );

    // Create (but don't write) the meta.far
    d.new_file(
        "install/pkg/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
        0600,
    )
    .expect("create install file");

    assert_error_kind!(
        d.metadata("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93")
            .map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );

    assert_eq!(
        ls_simple(d.list_dir("needs/packages").expect("list dir")).expect("list dir contents"),
        ["b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"]
    );

    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
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
    assert_error_kind!(
        ls_simple(d.list_dir("system").unwrap()),
        io::ErrorKind::Other, // Not supported
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
async fn test_pkgfs_install_update() {
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
        d.remove_dir("ctl/garbage").unwrap();

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
async fn test_pkgfs_with_cache_index() {
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

    verify_contents(&system_image_package, subdir_proxy(&d, "system"))
        .await
        .expect("valid system_image");

    assert_eq!(sorted(ls(&pkgfs, "packages").unwrap()), ["example", "system_image"]);

    verify_contents(&pkg, subdir_proxy(&d, "packages/example/0"))
        .await
        .expect("valid example package");

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
        d.remove_dir("ctl/garbage").unwrap();

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

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_restart_reveals_shadowed_cache_package() {
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

    let pkg2 = PackageBuilder::new("example")
        .add_resource_at("a/b", "Hello world 2!\n".as_bytes())
        .build()
        .await
        .expect("build package");
    install(&pkgfs, &pkg2);

    verify_contents(&pkg2, subdir_proxy(&d, "packages/example/0"))
        .await
        .expect("pkg2 replaced pkg");

    // cache version is inaccessible
    assert_error_kind!(
        d.metadata(&format!("versions/{}", pkg.meta_far_merkle_root())).map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );

    drop(d);
    let pkgfs = pkgfs.restart().unwrap();
    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    // cache version is accessible again.
    verify_contents(&pkg, subdir_proxy(&d, "packages/example/0")).await.unwrap();
    verify_contents(&pkg, subdir_proxy(&d, &format!("versions/{}", pkg.meta_far_merkle_root())))
        .await
        .unwrap();

    // updated version is gone
    assert_error_kind!(
        d.metadata(&format!("versions/{}", pkg2.meta_far_merkle_root())).map(|m| m.is_dir()),
        io::ErrorKind::NotFound
    );

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
        "b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93";
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
                "install/pkg/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
                0600,
            )
            .expect("create install file");
        to_write.set_len(meta_far.metadata().unwrap().len()).expect("set_len meta.far");
        std::io::copy(&mut meta_far, &mut to_write).expect("write meta.far");
    }
    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
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
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
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
    d.open_file("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93/a/b")
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

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_packages_dynamic_packages_allowlist_succeeds() {
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

    // We've put the 'example' package on the allowlist, so we should be
    // able to read files from it through pkgfs/packages
    let mut file_contents = String::new();
    d.open_file("packages/example/0/a/b")
        .expect("read package file1")
        .read_to_string(&mut file_contents)
        .expect("read package file2");
    assert_eq!(&file_contents, "Hello world!\n");

    // We should be able to see our package in the /packages directory
    assert_eq!(
        sorted(ls_simple(d.list_dir("packages").expect("list dir")).expect("list dir contents")),
        ["example", "system_image"]
    );

    drop(d);
    pkgfs.stop().await.expect("shutting down pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_packages_dynamic_packages_allowlist_fails() {
    let pkg = example_package().await;
    let system_image_package = SystemImageBuilder::new()
        .cache_packages(&[&pkg])
        .pkgfs_non_static_packages_allowlist(&["not_example"])
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

    // Test the 'Open' path - we shouldn't be able to open a file in our
    // package through /packages.
    // The 'example' package wasn't in the allowlist but is on the disk,
    // so we expect this to fail with a permission error, which is bubbled up as Other.
    assert_eq!(
        d.open_file("packages/example/0/a/b")
            .expect_err("shouldn't be able to open file in non-allowlisted package")
            .kind(),
        std::io::ErrorKind::Other
    );

    // The not_on_disk package wasn't in the allowlist but _isn't_ on the disk,
    // so we expect this to fail with a NotFound.
    assert_eq!(
        d.open_file("packages/not_on_disk/0/a/b")
            .expect_err("shouldn't be able to open file in package not on disk")
            .kind(),
        std::io::ErrorKind::NotFound
    );

    // Test the 'Read' path for /packages - listing
    // We should *not* be able to see our package in the /packages directory
    assert_eq!(
        // This doesn't have to be sorted, but leaving it as sorted so anyone who modifies this doesn't trip over the possible bug.
        sorted(ls_simple(d.list_dir("packages").expect("list dir")).expect("list dir contents")),
        ["system_image"] // We should not see the 'example' package, since it's a dynamic package that's not on the allowlist.
    );

    drop(d);
    pkgfs.stop().await.expect("shutting down pkgfs");
}

// Test the enforcement flag of `pkgfs`. If set to false, it should return dynamic packages
// from /pkgfs/packages even if they aren't on the allowlist.
#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_packages_dynamic_packages_allowlist_enforcement_flag() {
    let pkg = example_package().await;
    let system_image_package = SystemImageBuilder::new()
        .cache_packages(&[&pkg])
        .pkgfs_non_static_packages_allowlist(&["not_example"])
        .build()
        .await;

    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());

    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .enforce_packages_non_static_allowlist(false) // turn off allowlist enforcement
        .start()
        .unwrap();
    let d = pkgfs.root_dir().expect("getting pkgfs root dir");

    let mut file_contents = String::new();
    d.open_file("packages/example/0/a/b")
        .expect("read package file1")
        .read_to_string(&mut file_contents)
        .expect("read package file2");
    assert_eq!(&file_contents, "Hello world!\n");

    // We should be able to see our package in the /packages directory
    assert_eq!(
        sorted(ls_simple(d.list_dir("packages").expect("list dir")).expect("list dir contents")),
        ["example", "system_image"]
    );

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
    pkgdir: pkgfs::package::Directory,
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
            // See //garnet/go/src/thinfs/zircon/rpc/rpc.go's errorToZx for why fs.ErrPermission maps
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
async fn executability_enforcement_blocks_cache_package() {
    let pkg = executable_package().await;
    let system_image_package = SystemImageBuilder::new().cache_packages(&[&pkg]).build().await;

    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .enforce_non_base_executability_restrictions(true)
        .enforce_packages_non_static_allowlist(false)
        .start()
        .unwrap();

    verify_executable_package_rights(&pkgfs, &pkg, Executability::BlockExecute).await;

    pkgfs.stop().await.expect("shutting down pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn executability_enforcement_defaults_to_on() {
    let pkg = executable_package().await;
    let system_image_package = SystemImageBuilder::new().cache_packages(&[&pkg]).build().await;

    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .enforce_packages_non_static_allowlist(false)
        .start()
        .unwrap();

    verify_executable_package_rights(&pkgfs, &pkg, Executability::BlockExecute).await;

    pkgfs.stop().await.expect("shutting down pkgfs");
}

#[fasync::run_singlethreaded(test)]
async fn executability_enforcement_allows_allowlisted_package() {
    let pkg = executable_package().await;
    let system_image_package = SystemImageBuilder::new()
        .cache_packages(&[&pkg])
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

#[fasync::run_singlethreaded(test)]
async fn open_executable_allowed_when_statically_disabled() {
    let pkg = executable_package().await;
    let system_image_package = SystemImageBuilder::new()
        .cache_packages(&[&pkg])
        .pkgfs_disable_executability_restrictions()
        .build()
        .await;

    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());

    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .enforce_non_base_executability_restrictions(true)
        .enforce_packages_non_static_allowlist(false)
        .start()
        .unwrap();
    verify_executable_package_rights(&pkgfs, &pkg, Executability::AllowExecute).await;

    let pkgfs = pkgfs
        .into_builder()
        .unwrap()
        .enforce_non_base_executability_restrictions(false)
        .enforce_packages_non_static_allowlist(false)
        .start()
        .unwrap();
    verify_executable_package_rights(&pkgfs, &pkg, Executability::AllowExecute).await;

    pkgfs.stop().await.expect("shutting down pkgfs");
}

// A previous version of pkgfs/thinfs crashed on a call to node_get_flags on the /pkg directory, since node_get_flags
// was an unimplemented transitional method.
// TODO(fxbug.dev/55663): remove this test when unimplemented transitional methods do not crash the server.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_pkgfs_node_get_flags() {
    // Try node_get_flags on our own package directory.
    // thinfs returns an error for this call, which closes the channel.
    let this_pkg_dir = io_util::open_directory_in_namespace("/pkg", io_util::OPEN_RIGHT_READABLE)
        .expect("opening /pkg");
    let (status, flags) = this_pkg_dir.node_get_flags().await.expect("getting directory flags");
    assert_eq!(status, Status::NOT_SUPPORTED.into_raw());
    assert_eq!(flags, 0);

    // Try node_get_flags on a file within our package directory.
    // thinfs maps NodeGetFlags to GetFlags, so this should not close the channel.
    let meta_far_file_proxy =
        io_util::open_file_in_namespace("/pkg/meta", io_util::OPEN_RIGHT_READABLE)
            .expect("opening /pkg/meta as file");
    let (zx_result, flags) =
        meta_far_file_proxy.node_get_flags().await.expect("getting file flags");
    assert_eq!(zx_result, Status::OK.into_raw());
    assert_eq!(flags, io_util::OPEN_RIGHT_READABLE);

    // We should still be able to read our own package directory and read our own merkle root,
    // which means pkgfs hasn't crashed.
    assert!(fs::read_to_string("/pkg/meta").expect("read to string").len() > 0);
}

// A previous version of pkgfs/thinfs crashed on a call to node_set_flags, since node_set_flags
// was an unimplemented transitional method.
// TODO(fxbug.dev/55663): remove this test when unimplemented transitional methods do not crash the server.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_pkgfs_node_set_flags() {
    // Try node_set_flags on our own package directory.
    // thinfs returns an error for this call, which closes the channel.
    let this_pkg_dir = io_util::open_directory_in_namespace("/pkg", io_util::OPEN_RIGHT_READABLE)
        .expect("opening /pkg");
    let status = this_pkg_dir
        .node_set_flags(fidl_fuchsia_io::OPEN_FLAG_APPEND)
        .await
        .expect("setting directory flags");
    assert_eq!(status, Status::NOT_SUPPORTED.into_raw());

    // Try node_set_flags on a file within our package directory.
    // thinfs returns an error for this call, which closes the channel
    let meta_far_file_proxy =
        io_util::open_file_in_namespace("/pkg/meta", io_util::OPEN_RIGHT_READABLE)
            .expect("opening /pkg/meta as file");
    let status = meta_far_file_proxy
        .node_set_flags(fidl_fuchsia_io::OPEN_FLAG_APPEND)
        .await
        .expect("setting file flags");
    assert_eq!(status, Status::NOT_SUPPORTED.into_raw());

    // We should still be able to read our own package directory and read our own merkle root,
    // which means pkgfs hasn't crashed.
    assert!(fs::read_to_string("/pkg/meta").expect("read to string").len() > 0);
}
