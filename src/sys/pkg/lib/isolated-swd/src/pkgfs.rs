// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fdio::{SpawnAction, SpawnOptions},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon as zx,
    scoped_task::{self, Scoped},
    std::ffi::CString,
};

const PKGSVR_PATH: &str = "/pkg/bin/pkgsvr";

/// Represents the sandboxed pkgfs.
pub struct Pkgfs {
    _process: Scoped,
    root: DirectoryProxy,
}

impl Pkgfs {
    /// Launch pkgfs using the given blobfs as the backing blob store.
    pub fn launch(blobfs: ClientEnd<DirectoryMarker>) -> Result<Self, Error> {
        Pkgfs::launch_with_args(blobfs, true)
    }

    /// Launch pkgfs using the given blobfs as the backing blob store.
    /// If enforce_non_static_allowlist is false, will disable the non-static package allowlist
    /// (for use in tests).
    fn launch_with_args(
        blobfs: ClientEnd<DirectoryMarker>,
        enforce_non_static_allowlist: bool,
    ) -> Result<Self, Error> {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;
        let handle_info = HandleInfo::new(HandleType::User0, 0);

        // we use a scoped_task to prevent the pkgfs hanging around
        // if our process dies.
        let pkgsvr = scoped_task::spawn_etc(
            scoped_task::job_default(),
            SpawnOptions::CLONE_ALL,
            &CString::new(PKGSVR_PATH).unwrap(),
            &[
                &CString::new(PKGSVR_PATH).unwrap(),
                &CString::new(format!(
                    "--enforcePkgfsPackagesNonStaticAllowlist={}",
                    enforce_non_static_allowlist
                ))
                .unwrap(),
            ],
            None,
            &mut [
                SpawnAction::add_handle(handle_info, server_end.into_channel().into()),
                SpawnAction::add_namespace_entry(
                    &CString::new("/blob").unwrap(),
                    blobfs.into_channel().into(),
                ),
            ],
        )
        .map_err(|(status, _)| status)
        .context("spawn_etc failed")?;

        Ok(Pkgfs { _process: pkgsvr, root: proxy })
    }

    /// Get a handle to the root directory of the pkgfs.
    pub fn root_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        let (root_clone, server_end) = zx::Channel::create()?;
        self.root.clone(fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS, server_end.into())?;
        Ok(root_clone.into())
    }
}

pub mod for_tests {
    use {
        super::*,
        blobfs_ramdisk::BlobfsRamdisk,
        fuchsia_pkg_testing::Package,
        matches::assert_matches,
        pkgfs::{
            self,
            install::{BlobCreateError, BlobKind, BlobWriteSuccess},
        },
        std::io::Read,
    };

    /// This wraps `Pkgfs` in order to reduce test boilerplate.
    pub struct PkgfsForTest {
        pub blobfs: BlobfsRamdisk,
        pub pkgfs: Pkgfs,
    }

    impl PkgfsForTest {
        /// Launch pkgfs. The pkgsvr binary must be located at /pkg/bin/pkgsvr.
        pub fn new() -> Result<Self, Error> {
            let blobfs = BlobfsRamdisk::start().context("starting blobfs")?;
            let pkgfs = Pkgfs::launch_with_args(
                blobfs.root_dir_handle().context("getting blobfs root handle")?,
                false,
            )
            .context("launching pkgfs")?;
            Ok(PkgfsForTest { blobfs, pkgfs })
        }

        pub fn root_proxy(&self) -> Result<DirectoryProxy, Error> {
            Ok(self.pkgfs.root_handle()?.into_proxy()?)
        }
    }

    /// Install the given package to pkgfs.
    pub async fn install_package(root: &DirectoryProxy, pkg: &Package) -> Result<(), Error> {
        let installer =
            pkgfs::install::Client::open_from_pkgfs_root(root).context("Opening pkgfs")?;

        // install the meta far
        let mut buf = vec![];
        pkg.meta_far().unwrap().read_to_end(&mut buf)?;
        let merkle = pkg.meta_far_merkle_root().to_owned();
        let (blob, closer) = installer.create_blob(merkle, BlobKind::Package).await?;
        let blob = blob.truncate(buf.len() as u64).await?;
        assert_matches!(blob.write(&buf[..]).await, Ok(BlobWriteSuccess::Done));
        closer.close().await;

        // install the blobs in the package
        for mut blob in pkg.content_blob_files() {
            let mut buf = vec![];
            blob.file.read_to_end(&mut buf).unwrap();
            let blob_result = match installer.create_blob(blob.merkle, BlobKind::Data).await {
                Ok((blob, closer)) => Ok(Some((blob, closer))),
                Err(BlobCreateError::AlreadyExists) => Ok(None),
                Err(e) => Err(e),
            }?;

            if let Some((blob, closer)) = blob_result {
                let blob = blob.truncate(buf.len() as u64).await?;
                assert_matches!(blob.write(&buf[..]).await, Ok(BlobWriteSuccess::Done));
                closer.close().await;
            }
        }
        Ok(())
    }
}

#[cfg(test)]
pub mod tests {
    #[cfg(test)]
    use fuchsia_pkg_testing::PackageBuilder;
    use {
        super::for_tests::{install_package, PkgfsForTest},
        super::*,
        fuchsia_async as fasync,
    };

    #[fasync::run_singlethreaded(test)]
    pub async fn test_pkgfs_install() -> Result<(), Error> {
        let pkgfs = PkgfsForTest::new()?;

        let name = "pkgfs_install";
        let package = PackageBuilder::new(name)
            .add_resource_at("data/file1", "file with some test data".as_bytes())
            .add_resource_at("data/file2", "file with some test data".as_bytes())
            .add_resource_at("data/file3", "third, totally different file".as_bytes())
            .build()
            .await
            .context("Building package")?;
        install_package(&pkgfs.root_proxy()?, &package).await?;

        let client = pkgfs::packages::Client::open_from_pkgfs_root(&pkgfs.root_proxy()?)?;
        let dir = client.open_package(name, None).await?;
        package.verify_contents(&dir.into_proxy()).await.unwrap();

        Ok(())
    }
}
