// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around the /pkgfs/versions filesystem.

use {
    fidl::{
        endpoints::{Proxy, RequestStream},
        AsHandleRef,
    },
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryObject, DirectoryProxy, DirectoryRequest, DirectoryRequestStream,
        NodeInfo,
    },
    fuchsia_hash::Hash,
    fuchsia_zircon::Status,
    futures::prelude::*,
};

pub use crate::packages::OpenError;

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

    /// Creates a new client backed by the returned request stream. This constructor should not be
    /// used outside of tests.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn new_test() -> (Self, DirectoryRequestStream) {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DirectoryMarker>().unwrap();

        (Self { proxy }, stream)
    }

    /// Creates a new client backed by the returned mock. This constructor should not be used
    /// outside of tests.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn new_mock() -> (Self, Mock) {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DirectoryMarker>().unwrap();

        (Self { proxy }, Mock { stream })
    }

    /// Open the package given by `meta_far_merkle`. Verifies the OnOpen event before returning.
    pub async fn open_package(
        &self,
        meta_far_merkle: &Hash,
    ) -> Result<fuchsia_pkg::PackageDirectory, OpenError> {
        // TODO(fxbug.dev/37858) allow opening as executable too
        let flags = fidl_fuchsia_io::OPEN_RIGHT_READABLE;
        let dir =
            io_util::directory::open_directory(&self.proxy, &meta_far_merkle.to_string(), flags)
                .await
                .map_err(|e| match e {
                    io_util::node::OpenError::OpenError(Status::NOT_FOUND) => OpenError::NotFound,
                    other => OpenError::Io(other),
                })?;

        Ok(fuchsia_pkg::PackageDirectory::from_proxy(dir))
    }
}

/// A testing server implementation of /pkgfs/versions.
///
/// Mock does not handle requests until instructed to do so.
pub struct Mock {
    stream: DirectoryRequestStream,
}

impl Mock {
    /// Consume the next directory request, verifying it is intended to open the package identified
    /// by `merkle`.  Returns a `MockPackage` representing the open package directory.
    ///
    /// # Panics
    ///
    /// Panics on error or assertion violation (unexpected requests or a mismatched open call)
    pub async fn expect_open_package(&mut self, merkle: Hash) -> MockPackage {
        match self.stream.next().await {
            Some(Ok(DirectoryRequest::Open {
                flags: _,
                mode: _,
                path,
                object,
                control_handle: _,
            })) => {
                assert_eq!(path, merkle.to_string());

                let stream = object.into_stream().unwrap().cast_stream();
                MockPackage { stream }
            }
            other => panic!("unexpected request: {:?}", other),
        }
    }

    /// Asserts that the request stream closes without any further requests.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub async fn expect_done(mut self) {
        match self.stream.next().await {
            None => {}
            Some(request) => panic!("unexpected request: {:?}", request),
        }
    }
}

/// A testing server implementation of an open /pkgfs/versions/<merkle> directory.
///
/// MockBlob does not send the OnOpen event or handle requests until instructed to do so.
pub struct MockPackage {
    stream: DirectoryRequestStream,
}

impl MockPackage {
    fn send_on_open(&mut self, status: Status) {
        let mut info = NodeInfo::Directory(DirectoryObject);
        let () =
            self.stream.control_handle().send_on_open_(status.into_raw(), Some(&mut info)).unwrap();
    }

    /// Fail the open request with an error indicating the package does not exist.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub async fn fail_open_with_not_found(mut self) {
        self.send_on_open(Status::NOT_FOUND);
    }

    /// Succeeds the open request.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub async fn succeed_open(mut self) -> Self {
        self.send_on_open(Status::OK);
        self
    }

    /// Consume the next directory request, verifying it is a request to duplicate this package
    /// onto another handle.  Returns a `MockPackage` representing the duplicated package
    /// directory.
    ///
    /// # Panics
    ///
    /// Panics on error or assertion violation (unexpected requests or a mismatched clone call)
    pub async fn expect_clone(&mut self) -> MockPackage {
        match self.stream.next().await {
            Some(Ok(DirectoryRequest::Clone { flags: _, object, control_handle: _ })) => {
                let stream = object.into_stream().unwrap().cast_stream();
                MockPackage { stream }
            }
            other => panic!("unexpected request: {:?}", other),
        }
    }

    /// Verify this directory's channel is the server end for the given proxy dir, consuming both
    /// objects.
    ///
    /// # Panics
    ///
    /// Panics on error or if the objects are not using the same channel.
    pub fn verify_are_same_channel(self, proxy: DirectoryProxy) {
        let expected_server_end_koid = proxy.as_channel().basic_info().unwrap().related_koid;
        let actual_server_end_koid = self.stream.into_inner().0.channel().get_koid().unwrap();

        assert_eq!(expected_server_end_koid, actual_server_end_koid);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::install::BlobKind, assert_matches::assert_matches,
        fuchsia_pkg_testing::PackageBuilder, pkgfs_ramdisk::PkgfsRamdisk,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_non_existant_package_fails() {
        let pkgfs = PkgfsRamdisk::start().unwrap();
        let root = pkgfs.root_dir_proxy().unwrap();
        let client = Client::open_from_pkgfs_root(&root).unwrap();

        let merkle = fuchsia_merkle::MerkleTree::from_reader(std::io::empty()).unwrap().root();
        assert_matches!(client.open_package(&merkle).await, Err(OpenError::NotFound));

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
        assert_matches!(client.open_package(&pkg_merkle).await, Err(OpenError::NotFound));

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
