// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_pkg_testing::{Package, PackageBuilder, VerificationError},
    pkgfs_ramdisk::PkgfsRamdisk,
    std::{
        collections::HashSet,
        fmt::Debug,
        future::Future,
        io::{self, Read, Write},
    },
};

mod gc_test;
mod get_buffer;
mod pkgfs_test;

trait AsRootDir {
    fn as_root_dir(&self) -> openat::Dir;
}

impl AsRootDir for PkgfsRamdisk {
    fn as_root_dir(&self) -> openat::Dir {
        self.root_dir().expect("getting pkgfs root dir")
    }
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

async fn example_package() -> Package {
    PackageBuilder::new("example")
        .add_resource_at("a/b", "Hello world!\n".as_bytes())
        .build()
        .await
        .expect("build package")
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

fn ls(root: &dyn AsRootDir, path: impl openat::AsPath) -> Result<Vec<String>, io::Error> {
    let d = root.as_root_dir();
    ls_simple(d.list_dir(path)?)
}

fn ls_simple(d: openat::DirIter) -> Result<Vec<String>, io::Error> {
    Ok(d.map(|i| i.map(|entry| entry.file_name().to_string_lossy().into()))
        .collect::<Result<Vec<_>, _>>()?)
}

/// Allows us to call sort inline
fn sorted<T: Ord>(mut vec: Vec<T>) -> Vec<T> {
    vec.sort();
    vec
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

fn verify_contents<'a>(
    pkg: &'a Package,
    dir: fidl_fuchsia_io::DirectoryProxy,
) -> impl Future<Output = Result<(), VerificationError>> + 'a {
    async move { pkg.verify_contents(&dir).await }
}

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

#[macro_export]
macro_rules! assert_error_kind {
    ($result:expr, $expected:expr) => {{
        assert_error_kind_helper($result, stringify!($result), $expected)
            .unwrap_or_else(|err_string| panic!("{}", err_string))
    }};
    ($result:expr, $expected:expr,) => {{
        assert_error_kind!($result, $expected)
    }};
}
