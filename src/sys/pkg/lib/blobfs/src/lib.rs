// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Typesafe wrappers around the /blob filesystem.

use {
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, DirectoryRequestStream, FileObject, FileProxy, NodeInfo,
    },
    fidl_fuchsia_io2::UnlinkOptions,
    fuchsia_hash::{Hash, ParseHashError},
    fuchsia_syslog::fx_log_warn,
    fuchsia_zircon::{self as zx, AsHandleRef as _, Status},
    futures::StreamExt as _,
    std::collections::HashSet,
    thiserror::Error,
};

pub mod blob;

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
        self.proxy
            .unlink2(&blob.to_string(), UnlinkOptions::EMPTY)
            .await?
            .map_err(|s| BlobfsError::Unlink(Status::from_raw(s)))
    }

    /// Open the blob for reading.
    pub async fn open_blob_for_read(
        &self,
        blob: &Hash,
    ) -> Result<FileProxy, io_util::node::OpenError> {
        io_util::directory::open_file(
            &self.proxy,
            &blob.to_string(),
            fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        )
        .await
    }

    /// Open a new blob for write.
    pub async fn open_blob_for_write(
        &self,
        blob: &Hash,
    ) -> Result<blob::Blob<blob::NeedsTruncate>, blob::CreateError> {
        blob::create(&self.proxy, blob).await
    }

    /// Returns whether blobfs has a blob with the given hash.
    pub async fn has_blob(&self, blob: &Hash) -> bool {
        let file = match io_util::directory::open_file_no_describe(
            &self.proxy,
            &blob.to_string(),
            fidl_fuchsia_io::OPEN_FLAG_DESCRIBE | fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        ) {
            Ok(file) => file,
            Err(_) => return false,
        };

        let mut events = file.take_event_stream();

        let fidl_fuchsia_io::FileEvent::OnOpen_ { s, info } = match events.next().await {
            Some(Ok(event)) => event,
            _ => return false,
        };

        if Status::from_raw(s) != Status::OK {
            return false;
        }

        let event = match info {
            Some(info) => match *info {
                NodeInfo::File(FileObject { event: Some(event), stream: None }) => event,
                _ => return false,
            },
            _ => return false,
        };

        // Check that the USER_0 signal has been asserted on the file's event to make sure we
        // return false on the edge case of the blob is current being written.
        match event.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST) {
            Ok(_) => true,
            Err(status) => {
                if status != Status::TIMED_OUT {
                    fx_log_warn!("blobfs: unknown error asserting blob existence: {}", status);
                }
                false
            }
        }
    }
}

#[cfg(test)]
impl Client {
    /// Constructs a new [`Client`] connected to the provided [`BlobfsRamdisk`]. Tests in this
    /// crate should use this constructor rather than [`BlobfsRamdisk::client`], which returns
    /// the non-cfg(test) build of this crate's [`blobfs::Client`]. While tests could use the
    /// [`blobfs::Client`] returned by [`BlobfsRamdisk::client`], it will be a different type than
    /// [`super::Client`], and the tests could not access its private members or any cfg(test)
    /// specific functionality.
    ///
    /// # Panics
    ///
    /// Panics on error.
    pub fn for_ramdisk(blobfs: &blobfs_ramdisk::BlobfsRamdisk) -> Self {
        Self::new(blobfs.root_dir_proxy().unwrap())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, blobfs_ramdisk::BlobfsRamdisk, fidl_fuchsia_io::DirectoryRequest,
        fuchsia_async as fasync, fuchsia_merkle::MerkleTree, futures::stream::TryStreamExt,
        maplit::hashset, matches::assert_matches, std::io::Write as _,
    };

    #[fasync::run_singlethreaded(test)]
    async fn list_known_blobs_empty() {
        let blobfs = BlobfsRamdisk::start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

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
        let client = Client::for_ramdisk(&blobfs);

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
        let client = Client::for_ramdisk(&blobfs);

        let merkle = MerkleTree::from_reader(&b"blob 1"[..]).unwrap().root();
        assert_matches!(client.delete_blob(&merkle).await, Ok(()));

        let expected = hashset! {MerkleTree::from_reader(&b"blob 2"[..]).unwrap().root()};
        assert_eq!(client.list_known_blobs().await.unwrap(), expected);
        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn delete_non_existing_blob() {
        let blobfs = BlobfsRamdisk::start().unwrap();
        let client = Client::for_ramdisk(&blobfs);
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
                DirectoryRequest::Unlink2 { name, responder, .. } => {
                    assert_eq!(name, blob_merkle.to_string());
                    responder.send(&mut Ok(())).unwrap();
                }
                other => panic!("unexpected request: {:?}", other),
            }
        })
        .detach();

        assert_matches!(client.delete_blob(&blob_merkle).await, Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn has_blob() {
        let blobfs = BlobfsRamdisk::builder().with_blob(&b"blob 1"[..]).start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

        assert_eq!(
            client.has_blob(&MerkleTree::from_reader(&b"blob 1"[..]).unwrap().root()).await,
            true
        );
        assert_eq!(client.has_blob(&Hash::from([1; 32])).await, false);

        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn has_blob_return_false_if_blob_is_partially_written() {
        let blobfs = BlobfsRamdisk::start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

        let blob = [3; 1024];
        let hash = MerkleTree::from_reader(&blob[..]).unwrap().root();

        let mut file = blobfs.root_dir().unwrap().write_file(hash.to_string(), 0o777).unwrap();
        assert_eq!(client.has_blob(&hash).await, false);
        file.set_len(blob.len() as u64).unwrap();
        assert_eq!(client.has_blob(&hash).await, false);
        file.write_all(&blob[..512]).unwrap();
        assert_eq!(client.has_blob(&hash).await, false);
        file.write_all(&blob[512..]).unwrap();
        assert_eq!(client.has_blob(&hash).await, true);

        blobfs.stop().await.unwrap();
    }
}
