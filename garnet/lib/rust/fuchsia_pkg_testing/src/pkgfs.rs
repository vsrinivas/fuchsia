// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Test utilities for starting a pkgfs server.

use {
    crate::{as_dir, as_file, blobfs::TestBlobFs, ProcessKillGuard},
    failure::{Error, ResultExt},
    fdio::{SpawnAction, SpawnOptions},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon as zx,
    fuchsia_zircon::Task,
    openat::Dir,
    std::{
        ffi::{CStr, CString},
        fs::File,
    },
};

/// A running pkgfs server.
///
/// Make sure to call TestPkgFs.stop() to shut it down properly and receive shutdown errors.
///
/// If dropped, only the ramdisk and dynamic index are deleted.
pub struct TestPkgFs {
    blobfs: TestBlobFs,
    proxy: DirectoryProxy,
    process: ProcessKillGuard,
    system_image_merkle: Option<String>,
}

impl TestPkgFs {
    /// Start a pkgfs server.
    pub fn start() -> Result<Self, Error> {
        let blobfs = TestBlobFs::start()?;
        TestPkgFs::start_with_blobfs(blobfs, Option::<String>::None)
    }

    /// Starts a package server backed by the provided blobfs.
    ///
    /// If system_image_merkle is Some, uses that as the starting system_image package.
    pub fn start_with_blobfs(
        blobfs: TestBlobFs,
        system_image_merkle: Option<impl Into<String>>,
    ) -> Result<Self, Error> {
        let system_image_merkle = system_image_merkle.map(|m| m.into());

        let pkgfs_root_handle_info = HandleInfo::new(HandleType::User0, 0);
        let (proxy, pkgfs_root_server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;

        let pkgsvr_bin = CString::new("/pkg/bin/pkgsvr").unwrap();
        let system_image_flag =
            system_image_merkle.as_ref().map(|s| CString::new(s.clone()).unwrap());

        let mut argv: Vec<&CStr> = vec![&pkgsvr_bin];
        if let Some(system_image_flag) = system_image_flag.as_ref() {
            argv.push(system_image_flag)
        }

        let process = fdio::spawn_etc(
            &fuchsia_runtime::job_default(),
            SpawnOptions::CLONE_ALL,
            &pkgsvr_bin,
            &argv,
            None,
            &mut [
                SpawnAction::add_handle(
                    pkgfs_root_handle_info,
                    pkgfs_root_server_end.into_channel().into(),
                ),
                SpawnAction::add_namespace_entry(
                    &CString::new("/blob").unwrap(),
                    blobfs.root_dir_handle().context("getting blobfs root dir handle")?.into(),
                ),
            ],
        )
        .map_err(|(status, _)| status)
        .context("spawning 'pkgsvr'")?;
        Ok(TestPkgFs { blobfs, proxy, process: process.into(), system_image_merkle })
    }

    /// Returns a new connection to the pkgfs root directory.
    pub fn root_dir_client_end(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        let (root_clone, server_end) = zx::Channel::create()?;
        self.proxy.clone(fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS, server_end.into())?;
        Ok(root_clone.into())
    }

    /// Returns a reference to the [`TestBlobFs`] backing this pkgfs.
    pub fn blobfs(&self) -> &TestBlobFs {
        &self.blobfs
    }

    /// Opens the root of pkgfs as a directory.
    pub fn root_dir(&self) -> Result<Dir, Error> {
        Ok(as_dir(self.root_dir_client_end()?))
    }

    /// Opens the root of pkgfs as a file.
    ///
    /// TODO: remove once there is an equivalent API to `add_dir_to_namespace` that doesn't
    /// accept a File.
    pub fn root_dir_file(&self) -> Result<File, Error> {
        Ok(as_file(self.root_dir_client_end()?))
    }

    /// Restarts PkgFs with the same backing blobfs.
    pub fn restart(self) -> Result<Self, Error> {
        self.process.kill().context("killing pkgfs")?;
        drop(self.proxy);
        TestPkgFs::start_with_blobfs(self.blobfs, self.system_image_merkle)
    }

    /// Shuts down the pkgfs server and all the backing infrastructure.
    ///
    /// This also shuts down blobfs and deletes the backing ramdisk.
    pub async fn stop(self) -> Result<(), Error> {
        self.process.kill().context("killing pkgfs")?;
        self.blobfs.stop().await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        crate::PackageBuilder,
        fuchsia_async as fasync,
        std::io::{Read, Write},
    };

    fn ls_simple(d: openat::DirIter) -> Result<Vec<String>, Error> {
        Ok(d.map(|i| i.map(|entry| entry.file_name().to_string_lossy().into()))
            .collect::<Result<Vec<_>, _>>()?)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_pkgfs() -> Result<(), Error> {
        let pkgfs = TestPkgFs::start().context("starting pkgfs")?;
        let blobfs_root_dir = pkgfs.blobfs().as_dir()?;
        let d = pkgfs.root_dir().context("getting pkgfs root dir")?;

        let pkg = PackageBuilder::new("example")
            .add_resource_at("a/b", "Hello world!\n".as_bytes())
            .build()
            .await
            .expect("build package");
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
        d.open_file(
            "versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93/a/b",
        )
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

        pkgfs.stop().await?;

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_pkgfs_with_system_image() {
        let pkg = PackageBuilder::new("example")
            .add_resource_at("a/b", "Hello world!\n".as_bytes())
            .build()
            .await
            .expect("build package");

        let system_image_package = crate::package::PackageBuilder::new("system_image")
            .add_resource_at(
                "data/static_packages",
                format!("example/0={}", pkg.meta_far_merkle_root()).as_bytes(),
            )
            .build()
            .await
            .unwrap();

        let blobfs = TestBlobFs::start().unwrap();
        system_image_package.write_to_blobfs(&blobfs);
        pkg.write_to_blobfs(&blobfs);

        let pkgfs = TestPkgFs::start_with_blobfs(
            blobfs,
            Some(system_image_package.meta_far_merkle_root().to_string()),
        )
        .expect("starting pkgfs");
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
}
