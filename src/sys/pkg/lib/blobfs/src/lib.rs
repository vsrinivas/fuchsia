// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]
#![warn(clippy::all)]

//! Typesafe wrappers around the /blob filesystem.

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio,
    fuchsia_hash::{Hash, ParseHashError},
    fuchsia_zircon::{self as zx, AsHandleRef as _, Status},
    futures::{stream, StreamExt as _},
    std::{collections::HashSet, sync::Arc},
    thiserror::Error,
    tracing::warn,
};

pub mod blob;
pub mod mock;
pub use mock::Mock;

/// Blobfs client errors.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum BlobfsError {
    #[error("while opening blobfs dir")]
    OpenDir(#[from] fuchsia_fs::node::OpenError),

    #[error("while listing blobfs dir")]
    ReadDir(#[source] fuchsia_fs::directory::Error),

    #[error("while deleting blob")]
    Unlink(#[source] Status),

    #[error("while sync'ing")]
    Sync(#[source] Status),

    #[error("while parsing blob merkle hash")]
    ParseHash(#[from] ParseHashError),

    #[error("FIDL error")]
    Fidl(#[from] fidl::Error),
}

/// Blobfs client
#[derive(Debug, Clone)]
pub struct Client {
    proxy: fio::DirectoryProxy,
}

impl Client {
    /// Returns an client connected to blobfs from the current component's namespace.
    pub fn open_from_namespace() -> Result<Self, BlobfsError> {
        let proxy = fuchsia_fs::directory::open_in_namespace(
            "/blob",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )?;
        Ok(Client { proxy })
    }

    /// Returns a client connected to blobfs from the current component's namespace with
    /// OPEN_RIGHT_READABLE and OPEN_RIGHT_EXECUTABLE.
    pub fn open_from_namespace_executable() -> Result<Self, BlobfsError> {
        let proxy = fuchsia_fs::directory::open_in_namespace(
            "/blob",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        )?;
        Ok(Client { proxy })
    }

    /// Returns a client connected to blobfs from the current component's namespace with
    /// OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE, OPEN_RIGHT_EXECUTABLE.
    pub fn open_from_namespace_rwx() -> Result<Self, BlobfsError> {
        let proxy = fuchsia_fs::directory::open_in_namespace(
            "/blob",
            fidl_fuchsia_io::OpenFlags::RIGHT_READABLE
                | fidl_fuchsia_io::OpenFlags::RIGHT_WRITABLE
                | fidl_fuchsia_io::OpenFlags::RIGHT_EXECUTABLE,
        )?;
        Ok(Client { proxy })
    }

    /// Returns an client connected to blobfs from the given blobfs root dir.
    pub fn new(proxy: fio::DirectoryProxy) -> Self {
        Client { proxy }
    }

    /// Creates a new client backed by the returned request stream. This constructor should not be
    /// used outside of tests.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn new_test() -> (Self, fio::DirectoryRequestStream) {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fio::DirectoryMarker>().unwrap();

        (Self { proxy }, stream)
    }

    /// Creates a new client backed by the returned mock. This constructor should not be used
    /// outside of tests.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn new_mock() -> (Self, mock::Mock) {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fio::DirectoryMarker>().unwrap();

        (Self { proxy }, mock::Mock { stream })
    }

    /// Creates a new *read-only* client backed by the returned `TempDirFake`.
    /// `TempDirFake` is a thin wrapper around a `tempfile::TempDir` and therefore *does not*
    /// replicate many of the important properties of blobfs, including:
    ///   * requiring truncate before write
    ///   * requiring file names be hashes of contents
    ///
    /// This should therefore generally only be used to test read-only clients of blobfs (i.e.
    /// tests in which only the test harness itself is writing to blobfs and the code-under-test is
    /// only reading from blobfs).
    ///
    /// To help enforce this, the DirectoryProxy used to create the `Client` is opened with only
    /// OPEN_RIGHT_READABLE.
    ///
    /// Requires a RW directory at "/tmp" in the component namespace.
    ///
    /// This constructor should not be used outside of tests.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn new_temp_dir_fake() -> (Self, TempDirFake) {
        let blobfs_dir = tempfile::TempDir::new().unwrap();
        let blobfs = Client::new(
            fuchsia_fs::directory::open_in_namespace(
                blobfs_dir.path().to_str().unwrap(),
                fio::OpenFlags::RIGHT_READABLE,
            )
            .unwrap(),
        );
        (blobfs, TempDirFake { dir: blobfs_dir })
    }

    /// Forward an open request directly to BlobFs.
    pub fn forward_open(
        &self,
        blob: &Hash,
        flags: fio::OpenFlags,
        mode: u32,
        server_end: ServerEnd<fio::NodeMarker>,
    ) -> Result<(), fidl::Error> {
        self.proxy.open(flags, mode, &blob.to_string(), server_end)
    }

    /// Returns the list of known blobs in blobfs.
    pub async fn list_known_blobs(&self) -> Result<HashSet<Hash>, BlobfsError> {
        let entries =
            fuchsia_fs::directory::readdir(&self.proxy).await.map_err(BlobfsError::ReadDir)?;

        entries
            .into_iter()
            .filter(|entry| entry.kind == fuchsia_fs::directory::DirentKind::File)
            .map(|entry| entry.name.parse().map_err(BlobfsError::ParseHash))
            .collect()
    }

    /// Delete the blob with the given merkle hash.
    pub async fn delete_blob(&self, blob: &Hash) -> Result<(), BlobfsError> {
        self.proxy
            .unlink(&blob.to_string(), fio::UnlinkOptions::EMPTY)
            .await?
            .map_err(|s| BlobfsError::Unlink(Status::from_raw(s)))
    }

    /// Open the blob for reading.
    pub async fn open_blob_for_read(
        &self,
        blob: &Hash,
    ) -> Result<fio::FileProxy, fuchsia_fs::node::OpenError> {
        fuchsia_fs::directory::open_file(
            &self.proxy,
            &blob.to_string(),
            fio::OpenFlags::RIGHT_READABLE,
        )
        .await
    }

    /// Open the blob for reading. The target is not verified to be any
    /// particular type and may not implement the fuchsia.io.File protocol.
    pub fn open_blob_for_read_no_describe(
        &self,
        blob: &Hash,
    ) -> Result<fio::FileProxy, fuchsia_fs::node::OpenError> {
        fuchsia_fs::directory::open_file_no_describe(
            &self.proxy,
            &blob.to_string(),
            fio::OpenFlags::RIGHT_READABLE,
        )
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
        let file = match fuchsia_fs::directory::open_file_no_describe(
            &self.proxy,
            &blob.to_string(),
            fio::OpenFlags::DESCRIBE | fio::OpenFlags::RIGHT_READABLE,
        ) {
            Ok(file) => file,
            Err(_) => return false,
        };

        let mut events = file.take_event_stream();

        let event = match events.next().await {
            None => return false,
            Some(event) => match event {
                Err(_) => return false,
                Ok(event) => match event {
                    fio::FileEvent::OnOpen_ { s, info } => {
                        if Status::from_raw(s) != Status::OK {
                            return false;
                        }

                        match info {
                            Some(info) => match *info {
                                fio::NodeInfoDeprecated::File(fio::FileObject {
                                    event: Some(event),
                                    stream: None,
                                }) => event,
                                _ => return false,
                            },
                            _ => return false,
                        }
                    }
                    fio::FileEvent::OnRepresentation { payload } => match payload {
                        fio::Representation::File(fio::FileInfo {
                            observer: Some(event), ..
                        }) => event,
                        _ => return false,
                    },
                },
            },
        };

        // Check that the USER_0 signal has been asserted on the file's event to make sure we
        // return false on the edge case of the blob is current being written.
        match event.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST) {
            Ok(_) => true,
            Err(status) => {
                if status != Status::TIMED_OUT {
                    warn!("blobfs: unknown error asserting blob existence: {}", status);
                }
                false
            }
        }
    }

    /// Determines which of candidate blobs exist and are readable in blobfs, returning the
    /// set difference of candidates and readable.
    pub async fn filter_to_missing_blobs(&self, candidates: &HashSet<Hash>) -> HashSet<Hash> {
        // This heuristic was taken from pkgfs. We are not sure how useful it is or why it was
        // added, however it is kept in out of an abundance of caution. We *suspect* the heuristic
        // is a performance optimization. Without the heuristic, we would always have to open every
        // candidate blob and see if it's readable, which may be expensive if there are many blobs.
        //
        // Note that if there are less than 20 blobs, we don't use the heuristic. This is because we
        // assume there is a certain threshold of number of blobs in a package where it is faster to
        // first do a readdir on blobfs to help rule out some blobs without having to open them. We
        // assume this threshold is 20. The optimal threshold is likely different between pkg-cache
        // and pkgfs, and, especially since this checks multiple blobs concurrently, we may not be
        // getting any benefits from the heuristic anymore.
        //
        // If you wish to remove this heuristic or change the threshold, consider doing a trace on
        // packages with varying numbers of blobs present/missing.
        // TODO(fxbug.dev/77717) re-evaluate filter_to_missing_blobs heuristic.
        let all_known_blobs =
            if candidates.len() > 20 { self.list_known_blobs().await.ok() } else { None };
        let all_known_blobs = Arc::new(all_known_blobs);

        stream::iter(candidates.clone())
            .map(move |blob| {
                let all_known_blobs = Arc::clone(&all_known_blobs);
                async move {
                    // We still need to check `has_blob()` even if the blob is in `all_known_blobs`,
                    // because it might not have been fully written yet.
                    if (*all_known_blobs).as_ref().map(|blobs| blobs.contains(&blob)) == Some(false)
                        || !self.has_blob(&blob).await
                    {
                        Some(blob)
                    } else {
                        None
                    }
                }
            })
            .buffer_unordered(50)
            .filter_map(|blob| async move { blob })
            .collect()
            .await
    }

    /// Call fuchsia.io/Node.Sync on the blobfs directory.
    pub async fn sync(&self) -> Result<(), BlobfsError> {
        self.proxy.sync().await?.map_err(zx::Status::from_raw).map_err(BlobfsError::Sync)
    }
}

/// `TempDirFake` is a thin wrapper around a `tempfile::TempDir` and therefore *does not*
/// replicate many of the important properties of blobfs, including:
///   * requiring truncate before write
///   * requiring file names be hashes of contents
///
/// This should therefore generally only be used to test read-only clients of blobfs (i.e.
/// tests in which only the test harness itself is writing to blobfs and the code-under-test is
/// only reading from blobfs).
pub struct TempDirFake {
    dir: tempfile::TempDir,
}

impl TempDirFake {
    /// Access the backing `tempfile::TempDir`.
    pub fn backing_temp_dir(&self) -> &tempfile::TempDir {
        &self.dir
    }

    /// Access the backing directory as an `openat::Dir`.
    ///
    /// # Panics
    ///
    /// Panics on error.
    pub fn backing_dir_as_openat_dir(&self) -> openat::Dir {
        let file = std::fs::File::open(self.dir.path()).unwrap();
        let handle = fdio::transfer_fd(file).unwrap();
        fdio::create_fd(handle).unwrap()
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
        super::*, assert_matches::assert_matches, blobfs_ramdisk::BlobfsRamdisk,
        fuchsia_async as fasync, fuchsia_merkle::MerkleTree, futures::stream::TryStreamExt,
        maplit::hashset, std::io::Write as _,
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
                fio::DirectoryRequest::Unlink { name, responder, .. } => {
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

    /// Wrapper for a blob and its hash. This lets the tests retain ownership of the Blob struct,
    /// which is important because it ensures blobfs will not close partially written blobs for the
    /// duration of the test.
    struct TestBlob<S> {
        _blob: blob::Blob<S>,
        hash: Hash,
    }

    async fn open_blob_only(client: &Client, blob: &[u8; 1024]) -> TestBlob<blob::NeedsTruncate> {
        let hash = MerkleTree::from_reader(&blob[..]).unwrap().root();
        let blob = client.open_blob_for_write(&hash).await.unwrap();
        TestBlob { _blob: blob, hash }
    }

    async fn open_and_truncate_blob(
        client: &Client,
        blob: &[u8; 1024],
    ) -> TestBlob<blob::NeedsData> {
        let hash = MerkleTree::from_reader(&blob[..]).unwrap().root();
        let blob = client
            .open_blob_for_write(&hash)
            .await
            .unwrap()
            .truncate(blob.len() as u64)
            .await
            .unwrap()
            .unwrap_needs_data();
        TestBlob { _blob: blob, hash }
    }

    async fn partially_write_blob(client: &Client, blob: &[u8; 1024]) -> TestBlob<blob::NeedsData> {
        let hash = MerkleTree::from_reader(&blob[..]).unwrap().root();
        let blob = client
            .open_blob_for_write(&hash)
            .await
            .unwrap()
            .truncate(blob.len() as u64)
            .await
            .unwrap()
            .unwrap_needs_data()
            .write(&blob[..512])
            .await
            .unwrap()
            .unwrap_more_to_write();
        TestBlob { _blob: blob, hash }
    }

    async fn fully_write_blob(client: &Client, blob: &[u8; 1024]) -> TestBlob<blob::AtEof> {
        let hash = MerkleTree::from_reader(&blob[..]).unwrap().root();
        let blob = client
            .open_blob_for_write(&hash)
            .await
            .unwrap()
            .truncate(blob.len() as u64)
            .await
            .unwrap()
            .unwrap_needs_data()
            .write(&blob[..])
            .await
            .unwrap()
            .unwrap_done();
        TestBlob { _blob: blob, hash }
    }

    #[fasync::run_singlethreaded(test)]
    async fn filter_to_missing_blobs_without_heuristic() {
        let blobfs = BlobfsRamdisk::builder().start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

        let missing_hash0 = Hash::from([0; 32]);
        let missing_hash1 = Hash::from([1; 32]);

        let present_blob0 = fully_write_blob(&client, &[2; 1024]).await;
        let present_blob1 = fully_write_blob(&client, &[3; 1024]).await;

        assert_eq!(
            client
                .filter_to_missing_blobs(
                    // Pass in <= 20 candidates so the heuristic is not used.
                    &hashset! { missing_hash0, missing_hash1,
                        present_blob0.hash, present_blob1.hash
                    }
                )
                .await,
            hashset! { missing_hash0, missing_hash1 }
        );

        blobfs.stop().await.unwrap();
    }

    /// Similar to the above test, except also test that partially written blobs count as missing.
    #[fasync::run_singlethreaded(test)]
    async fn filter_to_missing_blobs_without_heuristic_and_with_partially_written_blobs() {
        let blobfs = BlobfsRamdisk::builder().start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

        // Some blobs are created (but not yet truncated).
        let missing_blob0 = open_blob_only(&client, &[0; 1024]).await;

        // Some are truncated but not written.
        let missing_blob1 = open_and_truncate_blob(&client, &[1; 1024]).await;

        // Some are partially written.
        let missing_blob2 = partially_write_blob(&client, &[2; 1024]).await;

        // Some are fully written.
        let present_blob = fully_write_blob(&client, &[3; 1024]).await;

        assert_eq!(
            client
                .filter_to_missing_blobs(&hashset! {
                    missing_blob0.hash, missing_blob1.hash, missing_blob2.hash, present_blob.hash
                })
                .await,
            // All partially written blobs should count as missing.
            hashset! { missing_blob0.hash, missing_blob1.hash, missing_blob2.hash }
        );

        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn filter_to_missing_blobs_with_heuristic() {
        let blobfs = BlobfsRamdisk::builder().start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

        let missing_hash0 = Hash::from([0; 32]);
        let missing_hash1 = Hash::from([1; 32]);
        let missing_hash2 = Hash::from([2; 32]);
        let missing_hash3 = Hash::from([3; 32]);
        let missing_hash4 = Hash::from([4; 32]);
        let missing_hash5 = Hash::from([5; 32]);
        let missing_hash6 = Hash::from([6; 32]);
        let missing_hash7 = Hash::from([7; 32]);
        let missing_hash8 = Hash::from([8; 32]);
        let missing_hash9 = Hash::from([9; 32]);
        let missing_hash10 = Hash::from([10; 32]);

        let present_blob0 = fully_write_blob(&client, &[20; 1024]).await;
        let present_blob1 = fully_write_blob(&client, &[21; 1024]).await;
        let present_blob2 = fully_write_blob(&client, &[22; 1024]).await;
        let present_blob3 = fully_write_blob(&client, &[23; 1024]).await;
        let present_blob4 = fully_write_blob(&client, &[24; 1024]).await;
        let present_blob5 = fully_write_blob(&client, &[25; 1024]).await;
        let present_blob6 = fully_write_blob(&client, &[26; 1024]).await;
        let present_blob7 = fully_write_blob(&client, &[27; 1024]).await;
        let present_blob8 = fully_write_blob(&client, &[28; 1024]).await;
        let present_blob9 = fully_write_blob(&client, &[29; 1024]).await;
        let present_blob10 = fully_write_blob(&client, &[30; 1024]).await;

        assert_eq!(
            client
                .filter_to_missing_blobs(
                    // Pass in over 20 candidates to trigger the heuristic.
                    &hashset! { missing_hash0, missing_hash1, missing_hash2, missing_hash3,
                        missing_hash4, missing_hash5, missing_hash6, missing_hash7, missing_hash8,
                        missing_hash9, missing_hash10, present_blob0.hash, present_blob1.hash,
                        present_blob2.hash, present_blob3.hash, present_blob4.hash,
                        present_blob5.hash, present_blob6.hash, present_blob7.hash,
                        present_blob8.hash, present_blob9.hash, present_blob10.hash
                    }
                )
                .await,
            hashset! { missing_hash0, missing_hash1, missing_hash2, missing_hash3,
                missing_hash4, missing_hash5, missing_hash6, missing_hash7, missing_hash8,
                missing_hash9, missing_hash10
            }
        );

        blobfs.stop().await.unwrap();
    }

    /// Similar to the above test, except also test that partially written blobs count as missing.
    #[fasync::run_singlethreaded(test)]
    async fn filter_to_missing_blobs_with_heuristic_and_with_partially_written_blobs() {
        let blobfs = BlobfsRamdisk::builder().start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

        // Some blobs are created (but not yet truncated).
        let missing_blob0 = open_blob_only(&client, &[0; 1024]).await;
        let missing_blob1 = open_blob_only(&client, &[1; 1024]).await;
        let missing_blob2 = open_blob_only(&client, &[2; 1024]).await;

        // Some are truncated but not written.
        let missing_blob3 = open_and_truncate_blob(&client, &[3; 1024]).await;
        let missing_blob4 = open_and_truncate_blob(&client, &[4; 1024]).await;
        let missing_blob5 = open_and_truncate_blob(&client, &[5; 1024]).await;

        // Some are partially written.
        let missing_blob6 = partially_write_blob(&client, &[6; 1024]).await;
        let missing_blob7 = partially_write_blob(&client, &[7; 1024]).await;
        let missing_blob8 = partially_write_blob(&client, &[8; 1024]).await;

        // Some aren't even open.
        let missing_hash9 = Hash::from([9; 32]);
        let missing_hash10 = Hash::from([10; 32]);

        let present_blob0 = fully_write_blob(&client, &[20; 1024]).await;
        let present_blob1 = fully_write_blob(&client, &[21; 1024]).await;
        let present_blob2 = fully_write_blob(&client, &[22; 1024]).await;
        let present_blob3 = fully_write_blob(&client, &[23; 1024]).await;
        let present_blob4 = fully_write_blob(&client, &[24; 1024]).await;
        let present_blob5 = fully_write_blob(&client, &[25; 1024]).await;
        let present_blob6 = fully_write_blob(&client, &[26; 1024]).await;
        let present_blob7 = fully_write_blob(&client, &[27; 1024]).await;
        let present_blob8 = fully_write_blob(&client, &[28; 1024]).await;
        let present_blob9 = fully_write_blob(&client, &[29; 1024]).await;
        let present_blob10 = fully_write_blob(&client, &[30; 1024]).await;

        assert_eq!(
            client
                .filter_to_missing_blobs(
                    &hashset! { missing_blob0.hash, missing_blob1.hash, missing_blob2.hash,
                        missing_blob3.hash, missing_blob4.hash, missing_blob5.hash,
                        missing_blob6.hash, missing_blob7.hash, missing_blob8.hash,
                        missing_hash9, missing_hash10, present_blob0.hash,
                        present_blob1.hash, present_blob2.hash, present_blob3.hash,
                        present_blob4.hash, present_blob5.hash, present_blob6.hash,
                        present_blob7.hash, present_blob8.hash, present_blob9.hash,
                        present_blob10.hash
                    }
                )
                .await,
            // All partially written blobs should count as missing.
            hashset! { missing_blob0.hash, missing_blob1.hash, missing_blob2.hash,
                missing_blob3.hash, missing_blob4.hash, missing_blob5.hash, missing_blob6.hash,
                missing_blob7.hash, missing_blob8.hash, missing_hash9, missing_hash10
            }
        );

        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn sync() {
        let counter = Arc::new(std::sync::atomic::AtomicU32::new(0));
        let counter_clone = Arc::clone(&counter);
        let (client, mut stream) = Client::new_test();
        fasync::Task::spawn(async move {
            match stream.try_next().await.unwrap().unwrap() {
                fio::DirectoryRequest::Sync { responder } => {
                    counter_clone.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
                    responder.send(&mut Ok(())).unwrap();
                }
                other => panic!("unexpected request: {:?}", other),
            }
        })
        .detach();

        assert_matches!(client.sync().await, Ok(()));
        assert_eq!(counter.load(std::sync::atomic::Ordering::SeqCst), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn temp_dir_fake_backing_dir() {
        let (blobfs, fake) = Client::new_temp_dir_fake();

        std::fs::File::create(
            fake.backing_temp_dir()
                .path()
                .join("0000000000000000000000000000000000000000000000000000000000000000"),
        )
        .unwrap();

        let actual = blobfs.list_known_blobs().await.unwrap();

        assert_eq!(actual, hashset! {[0u8; 32].into()});
    }

    #[fasync::run_singlethreaded(test)]
    async fn temp_dir_fake_backing_dir_as_openat_dir() {
        let (blobfs, fake) = Client::new_temp_dir_fake();

        fake.backing_dir_as_openat_dir()
            .write_file("0000000000000000000000000000000000000000000000000000000000000000", 0o600)
            .unwrap();

        let actual = blobfs.list_known_blobs().await.unwrap();

        assert_eq!(actual, hashset! {[0u8; 32].into()});
    }

    #[fasync::run_singlethreaded(test)]
    async fn temp_dir_fake_read_only() {
        let (blobfs, _fake) = Client::new_temp_dir_fake();

        assert_matches!(
            blobfs.open_blob_for_write(&Hash::from([0; 32])).await,
            Err(blob::CreateError::AlreadyExists)
        );
    }
}
