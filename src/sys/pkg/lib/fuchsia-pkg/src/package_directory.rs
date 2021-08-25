// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around an open package directory.

use crate::{MetaContents, MetaContentsError, MetaPackage, MetaPackageError};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, FileProxy};
use fuchsia_hash::{Hash, ParseHashError};
use thiserror::Error;

// re-export wrapped io_util errors.
pub use io_util::{
    file::ReadError,
    node::{CloneError, CloseError, OpenError},
};

/// An error encountered while reading/parsing the package's hash
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum ReadHashError {
    #[error("while reading 'meta/'")]
    Read(#[source] ReadError),

    #[error("while parsing 'meta/'")]
    Parse(#[source] ParseHashError),
}

/// An error encountered while reading/parsing the package's meta/package file
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum LoadMetaPackageError {
    #[error("while reading 'meta/package'")]
    Read(#[source] ReadError),

    #[error("while parsing 'meta/package'")]
    Parse(#[source] MetaPackageError),
}

/// An error encountered while reading/parsing the package's meta/contents file
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum LoadMetaContentsError {
    #[error("while reading 'meta/contents'")]
    Read(#[source] ReadError),

    #[error("while parsing 'meta/contents'")]
    Parse(#[source] MetaContentsError),
}

/// An open package directory
#[derive(Debug, Clone)]
pub struct PackageDirectory {
    proxy: DirectoryProxy,
}

impl PackageDirectory {
    /// Interprets the provided directory proxy as a package dir.
    pub fn from_proxy(proxy: DirectoryProxy) -> Self {
        Self { proxy }
    }

    /// Creates a new channel pair, returning the client end as Self and the server end as a
    /// channel.
    pub fn create_request() -> Result<(Self, ServerEnd<DirectoryMarker>), fidl::Error> {
        let (proxy, request) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;
        Ok((Self::from_proxy(proxy), request))
    }

    /// Returns the current component's package directory.
    #[cfg(target_os = "fuchsia")]
    pub fn open_from_namespace() -> Result<Self, OpenError> {
        let dir =
            io_util::directory::open_in_namespace("/pkg", fidl_fuchsia_io::OPEN_RIGHT_READABLE)?;
        Ok(Self::from_proxy(dir))
    }

    /// Cleanly close the package directory, consuming self.
    pub async fn close(self) -> Result<(), CloseError> {
        io_util::directory::close(self.proxy).await
    }

    /// Send request to also serve this package directory on the given directory request.
    pub fn reopen(&self, dir_request: ServerEnd<DirectoryMarker>) -> Result<(), CloneError> {
        io_util::directory::clone_onto_no_describe(&self.proxy, None, dir_request)
    }

    /// Unwraps the inner DirectoryProxy, consuming self.
    pub fn into_proxy(self) -> DirectoryProxy {
        self.proxy
    }

    /// Open the file in the package given by `path` with the given access `rights`.
    pub async fn open_file(&self, path: &str, rights: OpenRights) -> Result<FileProxy, OpenError> {
        io_util::directory::open_file(&self.proxy, path, rights.to_flags()).await
    }

    async fn read_file_to_string(&self, path: &str) -> Result<String, ReadError> {
        let f = self.open_file(path, OpenRights::Read).await?;
        Ok(io_util::file::read_to_string(&f).await?)
    }

    /// Reads the merkle root of the package.
    pub async fn merkle_root(&self) -> Result<Hash, ReadHashError> {
        let merkle = self.read_file_to_string("meta").await.map_err(ReadHashError::Read)?;
        Ok(merkle.parse().map_err(ReadHashError::Parse)?)
    }

    /// Reads and parses the package's meta/contents file.
    pub async fn meta_contents(&self) -> Result<MetaContents, LoadMetaContentsError> {
        let meta_contents =
            self.read_file_to_string("meta/contents").await.map_err(LoadMetaContentsError::Read)?;
        let meta_contents = MetaContents::deserialize(meta_contents.as_bytes())
            .map_err(LoadMetaContentsError::Parse)?;
        Ok(meta_contents)
    }

    /// Reads and parses the package's meta/package file.
    pub async fn meta_package(&self) -> Result<MetaPackage, LoadMetaPackageError> {
        let meta_package =
            self.read_file_to_string("meta/package").await.map_err(LoadMetaPackageError::Read)?;
        let meta_package = MetaPackage::deserialize(meta_package.as_bytes())
            .map_err(LoadMetaPackageError::Parse)?;
        Ok(meta_package)
    }

    /// Returns an iterator of blobs needed by this package, does not include meta.far blob itself.
    /// Hashes may appear more than once.
    pub async fn blobs(&self) -> Result<impl Iterator<Item = Hash>, LoadMetaContentsError> {
        Ok(self.meta_contents().await?.into_hashes_undeduplicated())
    }
}

/// Possible open rights when opening a file within a package.
#[derive(Debug, Clone, PartialEq, Eq)]
#[allow(missing_docs)]
pub enum OpenRights {
    Read,
    ReadExecute,
}

impl OpenRights {
    fn to_flags(&self) -> u32 {
        match self {
            OpenRights::Read => fidl_fuchsia_io::OPEN_RIGHT_READABLE,
            OpenRights::ReadExecute => {
                fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_EXECUTABLE
            }
        }
    }
}

#[cfg(test)]
#[cfg(target_os = "fuchsia")]
mod tests {
    use super::*;
    use fidl::endpoints::Proxy;
    use matches::assert_matches;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_close() {
        let pkg = PackageDirectory::open_from_namespace().unwrap();
        let () = pkg.close().await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn reopen_is_new_connection() {
        let pkg = PackageDirectory::open_from_namespace().unwrap();

        let (proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
        assert_matches!(pkg.reopen(server_end), Ok(()));
        assert_matches!(PackageDirectory::from_proxy(proxy).close().await, Ok(()));

        pkg.into_proxy().into_channel().expect("no other users of the wrapped channel");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn merkle_root_is_pkg_meta() {
        let pkg = PackageDirectory::open_from_namespace().unwrap();

        let merkle: Hash = std::fs::read_to_string("/pkg/meta").unwrap().parse().unwrap();

        assert_eq!(pkg.merkle_root().await.unwrap(), merkle);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn list_blobs() {
        let pkg = PackageDirectory::open_from_namespace().unwrap();

        // listing blobs succeeds, and this package has some blobs.
        let blobs = pkg.blobs().await.unwrap().collect::<Vec<_>>();
        assert!(!blobs.is_empty());

        // the test duplicate blob appears twice
        let duplicate_blob_merkle =
            fuchsia_merkle::MerkleTree::from_reader("Hello World!".as_bytes()).unwrap().root();
        assert_eq!(blobs.iter().filter(|hash| *hash == &duplicate_blob_merkle).count(), 2);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn package_name_is_test_package_name() {
        let pkg = PackageDirectory::open_from_namespace().unwrap();

        assert_eq!(
            pkg.meta_package().await.unwrap().into_path().to_string(),
            "fuchsia-pkg-tests/0"
        );
    }
}
