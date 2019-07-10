// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Test utilities for starting a pkgfs server.

use {
    crate::{as_dir, blobfs::TestBlobFs},
    failure::{Error, ResultExt},
    fdio::{SpawnAction, SpawnOptions},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon as zx,
    fuchsia_zircon::Task,
    openat::Dir,
    std::ffi::CString,
    tempfile::TempDir,
};

/// A running pkgfs server.
///
/// Make sure to call TestPkgFs.stop() to shut it down properly and receive shutdown errors.
///
/// If dropped, only the ramdisk and dynamic index are deleted.
pub struct TestPkgFs {
    blobfs: TestBlobFs,
    index: TempDir,
    proxy: DirectoryProxy,
    process: zx::Process,
}

impl TestPkgFs {
    /// Start a pkgfs server.
    ///
    /// If index is Some, uses that as a dynamic index, otherwise uses an empty tempdir.
    pub fn start(index: impl Into<Option<TempDir>>) -> Result<Self, Error> {
        let blobfs = TestBlobFs::start()?;

        let index = match index.into() {
            Some(index) => index,
            None => TempDir::new().context("creating tempdir to use as dynamic package index")?,
        };
        TestPkgFs::start_with_blobfs(index, blobfs)
    }

    fn start_with_blobfs(index: TempDir, blobfs: TestBlobFs) -> Result<Self, Error> {
        let (connection, server_end) = zx::Channel::create()?;
        fdio::service_connect(index.path().to_str().expect("path is utf8"), server_end)
            .context("connecting to tempdir")?;

        let pkgfs_root_handle_info = HandleInfo::new(HandleType::User0, 0);
        let (proxy, pkgfs_root_server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;

        let process = fdio::spawn_etc(
            &fuchsia_runtime::job_default(),
            SpawnOptions::CLONE_ALL,
            &CString::new("/pkg/bin/pkgsvr").unwrap(),
            &[
                &CString::new("pkgsvr").unwrap(),
                &CString::new("-blob=/b").unwrap(),
                &CString::new("-index=/i").unwrap(),
            ],
            None,
            &mut [
                SpawnAction::add_handle(
                    pkgfs_root_handle_info,
                    pkgfs_root_server_end.into_channel().into(),
                ),
                SpawnAction::add_namespace_entry(
                    &CString::new("/b").unwrap(),
                    blobfs.root_dir_handle().context("getting blobfs root dir handle")?.into(),
                ),
                SpawnAction::add_namespace_entry(&CString::new("/i").unwrap(), connection.into()),
            ],
        )
        .map_err(|(status, _)| status)
        .context("spawning 'pkgsvr'")?;
        Ok(TestPkgFs { blobfs, index, proxy, process })
    }

    /// Returns a new connection to the pkgfs root directory.
    pub fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        let (root_clone, server_end) = zx::Channel::create()?;
        self.proxy.clone(fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS, server_end.into())?;
        Ok(root_clone.into())
    }

    /// Opens the root of pkgfs as a directory.
    pub fn root_dir(&self) -> Result<Dir, Error> {
        Ok(as_dir(self.root_dir_handle()?))
    }

    /// Opens the root of the backing blobfs as a directory.
    pub fn blobfs_root_dir(&self) -> Result<Dir, Error> {
        Ok(as_dir(self.blobfs.root_dir_handle()?))
    }

    /// Restarts PkgFs with the same backing blobfs and dynamic index.
    pub fn restart(self) -> Result<Self, Error> {
        self.process.kill().context("killing pkgfs")?;
        drop(self.proxy);
        TestPkgFs::start_with_blobfs(self.index, self.blobfs)
    }

    /// Shuts down the pkgfs server and all the backing infrastructure.
    ///
    /// This also shuts down blobfs and deletes the backing ramdisk and dynamic index.
    pub async fn stop(self) -> Result<(), Error> {
        self.process.kill().context("killing pkgfs")?;
        await!(self.blobfs.stop())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        crate::PackageBuilder,
        fuchsia_async as fasync,
        std::fs::read_dir,
        std::io::{Read, Write},
    };

    fn ls_simple(d: openat::DirIter) -> Result<Vec<String>, Error> {
        Ok(d.map(|i| i.map(|entry| entry.file_name().to_string_lossy().into()))
            .collect::<Result<Vec<_>, _>>()?)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_pkgfs() -> Result<(), Error> {
        let pkgfs = TestPkgFs::start(None).context("starting pkgfs")?;
        let blobfs_root_dir = pkgfs.blobfs_root_dir()?;
        let d = pkgfs.root_dir().context("getting pkgfs root dir")?;

        let pkg = await!(PackageBuilder::new("example")
            .add_resource_at("a/b", "Hello world!\n".as_bytes())
            .expect("add resource")
            .build())
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

        await!(pkgfs.stop())?;

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_pkgfs_with_index() -> Result<(), Error> {
        let index = TempDir::new().expect("create tempdir");
        let index_path = index.path().to_owned();
        let pkgfs = TestPkgFs::start(index).context("starting pkgfs")?;
        let blobfs_root_dir = pkgfs.blobfs_root_dir()?;
        let d = pkgfs.root_dir().context("getting pkgfs root dir")?;

        let pkg = await!(PackageBuilder::new("example")
            .add_resource_at("a/b", "Hello world!\n".as_bytes())
            .expect("add resource")
            .build())
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

        // package exists in the dynamic index
        assert_eq!(
            read_dir(index_path.join("packages"))
                .expect("read dynamic index")
                .map(|dir_ent| dir_ent.expect("read dir ent").file_name())
                .collect::<Vec<_>>(),
            ["example"],
        );

        drop(d);

        await!(pkgfs.stop())?;

        Ok(())
    }
}
