// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around the /pkgfs/versions filesystem.

use {
    crate::iou,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fuchsia_merkle::Hash,
    fuchsia_zircon::Status,
    thiserror::Error,
};

/// An error encountered while opening a package
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum PackageOpenError {
    #[error("the package does not exist")]
    NotFound,

    #[error("while opening the package: {}", _0)]
    Io(iou::OpenError),
}

/// An open handle to /pkgfs/versions
#[derive(Debug, Clone)]
pub struct Client {
    proxy: DirectoryProxy,
}

impl Client {
    /// Returns an client connected to pkgfs from the current component's namespace
    pub fn open_from_namespace() -> Result<Self, anyhow::Error> {
        let proxy = iou::open_directory_from_namespace("/pkgfs/versions")?;
        Ok(Client { proxy })
    }

    /// Returns an client connected to pkgfs from the given pkgfs root dir.
    pub fn open_from_pkgfs_root(pkgfs: &DirectoryProxy) -> Result<Self, anyhow::Error> {
        Ok(Client {
            proxy: iou::open_directory_no_describe(
                pkgfs,
                "versions",
                fidl_fuchsia_io::OPEN_RIGHT_READABLE,
            )?,
        })
    }

    /// Open the package given by `meta_far_merkle`, returning the directory and optionally cloning
    /// the directory onto the given `dir_request`.
    pub async fn open_package(
        &self,
        meta_far_merkle: Hash,
        dir_request: Option<ServerEnd<DirectoryMarker>>,
    ) -> Result<DirectoryProxy, PackageOpenError> {
        let flags = fidl_fuchsia_io::OPEN_RIGHT_READABLE;
        let dir = iou::open_directory(&self.proxy, &meta_far_merkle.to_string(), flags)
            .await
            .map_err(|e| match e {
                iou::OpenError::OpenError(Status::NOT_FOUND) => PackageOpenError::NotFound,
                other => PackageOpenError::Io(other),
            })?;

        // serve the directory on the client provided handle, if requested.
        if let Some(dir_request) = dir_request {
            let () = iou::clone_directory(&dir, dir_request).await.map_err(PackageOpenError::Io)?;
        }

        Ok(dir)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::install::BlobKind, fuchsia_pkg_testing::PackageBuilder,
        matches::assert_matches, pkgfs_ramdisk::PkgfsRamdisk,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_non_existant_package_fails() {
        let pkgfs = PkgfsRamdisk::start().unwrap();
        let root = pkgfs.root_dir_proxy().unwrap();
        let client = Client::open_from_pkgfs_root(&root).unwrap();

        let merkle = fuchsia_merkle::MerkleTree::from_reader(std::io::empty()).unwrap().root();
        assert_matches!(client.open_package(merkle, None).await, Err(PackageOpenError::NotFound));

        pkgfs.stop().await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_package_single_blob() {
        let pkgfs = PkgfsRamdisk::start().unwrap();
        let root = pkgfs.root_dir_proxy().unwrap();
        let install = crate::install::Client::open_from_pkgfs_root(&root).unwrap();
        let client = Client::open_from_pkgfs_root(&root).unwrap();

        let pkg = PackageBuilder::new("uniblob").build().await.unwrap();
        let pkg_merkle = pkg.meta_far_merkle_root().to_owned();
        install.write_meta_far(&pkg).await;

        assert_matches!(client.open_package(pkg_merkle, None).await, Ok(_));
        assert_matches!(
            pkg.verify_contents(&client.open_package(pkg_merkle, None).await.unwrap()).await,
            Ok(())
        );

        pkgfs.stop().await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_package_multiple_blobs() {
        let pkgfs = PkgfsRamdisk::start().unwrap();
        let root = pkgfs.root_dir_proxy().unwrap();
        let install = crate::install::Client::open_from_pkgfs_root(&root).unwrap();
        let client = Client::open_from_pkgfs_root(&root).unwrap();

        let pkg = PackageBuilder::new("multiblob")
            .add_resource_at("data/first", "contents of first blob".as_bytes())
            .add_resource_at("data/second", "contents of second blob".as_bytes())
            .build()
            .await
            .unwrap();
        let pkg_contents = pkg.meta_contents().unwrap().contents().to_owned();
        let pkg_merkle = pkg.meta_far_merkle_root().to_owned();
        install.write_meta_far(&pkg).await;

        // Package is not complete yet, so opening fails.
        assert_matches!(
            client.open_package(pkg_merkle, None).await,
            Err(PackageOpenError::NotFound)
        );

        install
            .write_blob(
                pkg_contents["data/first"],
                BlobKind::Data,
                "contents of first blob".as_bytes(),
            )
            .await;
        install
            .write_blob(
                pkg_contents["data/second"],
                BlobKind::Data,
                "contents of second blob".as_bytes(),
            )
            .await;

        assert_matches!(client.open_package(pkg_merkle, None).await, Ok(_));
        assert_matches!(
            pkg.verify_contents(&client.open_package(pkg_merkle, None).await.unwrap()).await,
            Ok(())
        );

        pkgfs.stop().await.unwrap();
    }
}
