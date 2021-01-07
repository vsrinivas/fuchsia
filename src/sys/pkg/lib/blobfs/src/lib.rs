// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Typesafe wrappers around the /blob filesystem.

use {
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, DirectoryRequestStream},
    fuchsia_hash::{Hash, ParseHashError},
    fuchsia_zircon::Status,
    std::collections::HashSet,
    thiserror::Error,
};

/// Blobfs client errors.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum BlobfsError {
    #[error("while opening blobfs dir")]
    OpenDir(#[from] io_util::node::OpenError),

    #[error("while listing blobfs dir")]
    ReadDir(#[source] files_async::Error),

    #[error("while deleting blob")]
    Unlink(#[source] Status),

    #[error("while parsing blob merkle hash")]
    ParseHash(#[from] ParseHashError),

    #[error("FIDL error")]
    Fidl(#[from] fidl::Error),
}

/// Blobfs client
#[derive(Debug, Clone)]
pub struct Client {
    proxy: DirectoryProxy,
}

impl Client {
    /// Returns an client connected to blobfs from the current component's namespace.
    pub fn open_from_namespace() -> Result<Self, BlobfsError> {
        let proxy = io_util::directory::open_in_namespace(
            "/blob",
            fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )?;
        Ok(Client { proxy })
    }

    /// Returns an client connected to blobfs from the given blobfs root dir.
    pub fn new(proxy: DirectoryProxy) -> Self {
        Client { proxy }
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

    /// Returns the list of known blobs in blobfs.
    pub async fn list_known_blobs(&self) -> Result<HashSet<Hash>, BlobfsError> {
        let entries = files_async::readdir(&self.proxy).await.map_err(BlobfsError::ReadDir)?;

        entries
            .into_iter()
            .filter(|entry| entry.kind == files_async::DirentKind::File)
            .map(|entry| entry.name.parse().map_err(BlobfsError::ParseHash))
            .collect()
    }

    /// Delete the blob with the given merkle hash.
    pub async fn delete_blob(&self, blob: &Hash) -> Result<(), BlobfsError> {
        let status = self.proxy.unlink(&blob.to_string()).await?;
        Status::ok(status).map_err(BlobfsError::Unlink)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, blobfs_ramdisk::BlobfsRamdisk, fidl_fuchsia_io::DirectoryRequest,
        fuchsia_async as fasync, fuchsia_merkle::MerkleTree, futures::stream::TryStreamExt,
        maplit::hashset, matches::assert_matches,
    };

    #[fasync::run_singlethreaded(test)]
    async fn list_known_blobs_empty() {
        let blobfs = BlobfsRamdisk::start().unwrap();
        let client = Client::new(blobfs.root_dir_proxy().unwrap());

        assert_eq!(client.list_known_blobs().await.unwrap(), HashSet::new());
        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn list_known_blobs() {
        let blobfs = BlobfsRamdisk::builder()
            .with_blob(&b"blob 1"[..])
            .with_blob(&b"blob 2"[..])
            .start()
            .unwrap();
        let client = Client::new(blobfs.root_dir_proxy().unwrap());

        let expected = blobfs.list_blobs().unwrap().into_iter().collect();
        assert_eq!(client.list_known_blobs().await.unwrap(), expected);
        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn delete_blob_and_then_list() {
        let blobfs = BlobfsRamdisk::builder()
            .with_blob(&b"blob 1"[..])
            .with_blob(&b"blob 2"[..])
            .start()
            .unwrap();
        let client = Client::new(blobfs.root_dir_proxy().unwrap());

        let merkle = MerkleTree::from_reader(&b"blob 1"[..]).unwrap().root();
        assert_matches!(client.delete_blob(&merkle).await, Ok(()));

        let expected = hashset! {MerkleTree::from_reader(&b"blob 2"[..]).unwrap().root()};
        assert_eq!(client.list_known_blobs().await.unwrap(), expected);
        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn delete_non_existing_blob() {
        let blobfs = BlobfsRamdisk::start().unwrap();
        let client = Client::new(blobfs.root_dir_proxy().unwrap());
        let blob_merkle = Hash::from([1; 32]);

        assert_matches!(
            client.delete_blob(&blob_merkle).await,
            Err(BlobfsError::Unlink(Status::NOT_FOUND))
        );
        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn delete_blob_mock() {
        let (client, mut stream) = Client::new_test();
        let blob_merkle = Hash::from([1; 32]);
        fasync::Task::spawn(async move {
            match stream.try_next().await.unwrap().unwrap() {
                DirectoryRequest::Unlink { path, responder } => {
                    assert_eq!(path, blob_merkle.to_string());
                    responder.send(Status::OK.into_raw()).unwrap();
                }
                other => panic!("unexpected request: {:?}", other),
            }
        })
        .detach();

        assert_matches!(client.delete_blob(&blob_merkle).await, Ok(()));
    }
}
