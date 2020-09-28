// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around the /pkgfs/versions filesystem.

use {crate::package, fidl_fuchsia_io::DirectoryProxy, fuchsia_hash::Hash, fuchsia_zircon::Status};

/// An open handle to /pkgfs/versions
#[derive(Debug, Clone)]
pub struct Client {
    proxy: DirectoryProxy,
}

impl Client {
    /// Returns an client connected to pkgfs from the current component's namespace
    pub fn open_from_namespace() -> Result<Self, io_util::node::OpenError> {
        let proxy = io_util::directory::open_in_namespace(
            "/pkgfs/versions",
            fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        )?;
        Ok(Client { proxy })
    }

    /// Returns an client connected to pkgfs from the given pkgfs root dir.
    pub fn open_from_pkgfs_root(pkgfs: &DirectoryProxy) -> Result<Self, io_util::node::OpenError> {
        Ok(Client {
            proxy: io_util::directory::open_directory_no_describe(
                pkgfs,
                "versions",
                fidl_fuchsia_io::OPEN_RIGHT_READABLE,
            )?,
        })
    }

    /// Open the package given by `meta_far_merkle`. Verifies the OnOpen event before returning.
    pub async fn open_package(
        &self,
        meta_far_merkle: &Hash,
    ) -> Result<package::Directory, package::OpenError> {
        // TODO(fxbug.dev/37858) allow opening as executable too
        let flags = fidl_fuchsia_io::OPEN_RIGHT_READABLE;
        let dir =
            io_util::directory::open_directory(&self.proxy, &meta_far_merkle.to_string(), flags)
                .await
                .map_err(|e| match e {
                    io_util::node::OpenError::OpenError(Status::NOT_FOUND) => {
                        package::OpenError::NotFound
                    }
                    other => package::OpenError::Io(other),
                })?;

        Ok(package::Directory::new(dir))
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
        assert_matches!(client.open_package(&merkle).await, Err(package::OpenError::NotFound));

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

        assert_matches!(client.open_package(&pkg_merkle).await, Ok(_));
        assert_matches!(
            pkg.verify_contents(&client.open_package(&pkg_merkle).await.unwrap().into_proxy())
                .await,
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
        assert_matches!(client.open_package(&pkg_merkle).await, Err(package::OpenError::NotFound));

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

        assert_matches!(client.open_package(&pkg_merkle).await, Ok(_));
        assert_matches!(
            pkg.verify_contents(&client.open_package(&pkg_merkle).await.unwrap().into_proxy())
                .await,
            Ok(())
        );

        pkgfs.stop().await.unwrap();
    }
}
