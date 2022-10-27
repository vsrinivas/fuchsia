// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around writing blobs to blobfs.

use {fidl_fuchsia_io as fio, fuchsia_hash::Hash, fuchsia_zircon::Status, thiserror::Error};

pub(crate) async fn create(
    blobfs: &fio::DirectoryProxy,
    hash: &Hash,
) -> Result<Blob<NeedsTruncate>, CreateError> {
    let flags =
        fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::RIGHT_READABLE;

    let proxy =
        fuchsia_fs::directory::open_file(blobfs, &hash.to_string(), flags).await.map_err(|e| {
            match e {
                fuchsia_fs::node::OpenError::OpenError(Status::ACCESS_DENIED) => {
                    CreateError::AlreadyExists
                }
                other => CreateError::Io(other),
            }
        })?;

    Ok(Blob { proxy, state: NeedsTruncate })
}

/// State for a blob that can be truncated.
#[derive(Debug)]
pub struct NeedsTruncate;

/// State for a blob that can be written to.
#[derive(Debug)]
pub struct NeedsData {
    size: u64,
    written: u64,
}

/// State for a blob that is present, readable, and has a seek position at the end.
#[derive(Debug)]
pub struct AtEof;

/// A blob in the process of being written.
#[derive(Debug)]
pub struct Blob<S> {
    proxy: fio::FileProxy,
    state: S,
}

/// A handle to a blob that must be explicitly closed to prevent future opens of the same blob from
/// racing with this blob closing.
#[derive(Debug)]
#[must_use = "Subsequent opens of this blob may race with closing this one"]
pub struct BlobCloser {
    proxy: fio::FileProxy,
    closed: bool,
}

/// The successful result of truncating a blob to its size.
#[derive(Debug)]
pub enum TruncateSuccess {
    /// The blob needs all its data written.
    NeedsData(Blob<NeedsData>),

    /// The blob is the empty blob and so does not need any data written.
    Done(fio::FileProxy),
}

/// The successful result of writing some data to a blob.
#[derive(Debug)]
pub enum WriteSuccess {
    /// There is still more data to write.
    MoreToWrite(Blob<NeedsData>),

    /// The blob is fully written.
    Done(Blob<AtEof>),
}

/// An error encountered while creating a blob
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum CreateError {
    #[error("the blob already exists or is being concurrently written")]
    AlreadyExists,

    #[error("while creating the blob")]
    Io(#[source] fuchsia_fs::node::OpenError),
}

/// An error encountered while truncating a blob
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum TruncateError {
    #[error("transport error")]
    Fidl(#[from] fidl::Error),

    #[error("insufficient storage space is available")]
    NoSpace,

    #[error("the blob is not the empty blob")]
    Corrupt,

    #[error("the blob is in the process of being written")]
    ConcurrentWrite,

    #[error("received unexpected failure status")]
    UnexpectedResponse(#[source] Status),
}

/// An error encountered while writing to a blob
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum WriteError {
    #[error("transport error")]
    Fidl(#[from] fidl::Error),

    #[error("file endpoint reported it wrote more bytes written than were actually provided to the file endpoint")]
    Overwrite,

    #[error("the written data was corrupt")]
    Corrupt,

    #[error("insufficient storage space is available")]
    NoSpace,

    #[error("received unexpected failure status")]
    UnexpectedResponse(#[source] Status),
}

/// An error encountered while reusing a written blob handle to read
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum IntoReadError {
    #[error("transport error")]
    Fidl(#[from] fidl::Error),

    #[error("received unexpected failure status")]
    UnexpectedResponse(#[from] Status),
}

impl<S> Blob<S> {
    fn with_state<T>(self, state: T) -> Blob<T> {
        Blob { proxy: self.proxy, state }
    }

    /// Closes the blob proxy, ignoring errors.  More importantly, if the blob isn't fully written,
    /// waits until the blob can again be opened for write.
    async fn close(self) {
        // Not much we can do with an error here, and some code paths will call this close() and
        // BlobCloser::close(), so some failures are expected.  Either way, we tried to close the
        // blob, and this method returns after blobfs's acknowledges the request (or we observe a
        // closed proxy).
        let _ = self.proxy.close().await;
    }
}

impl Blob<NeedsTruncate> {
    /// Returns an object that can be used to explicitly close the blob independently of what state
    /// this blob is in or when the returned object is dropped, whichever happens first.
    pub fn closer(&self) -> BlobCloser {
        BlobCloser { proxy: Clone::clone(&self.proxy), closed: false }
    }

    /// Truncates the blob to the given size. On success, the blob enters the writable state.
    pub async fn truncate(self, size: u64) -> Result<TruncateSuccess, TruncateError> {
        match self.proxy.resize(size).await?.map_err(Status::from_raw).map_err(
            |status| match status {
                Status::IO_DATA_INTEGRITY => TruncateError::Corrupt,
                Status::NO_SPACE => TruncateError::NoSpace,
                Status::BAD_STATE => TruncateError::ConcurrentWrite,
                status => TruncateError::UnexpectedResponse(status),
            },
        ) {
            Ok(()) => {}
            Err(err) => {
                // Dropping the file proxy will close the blob asynchronously. Make an explicit call to
                // blobfs to close the blob and wait for it to acknowledge that it closed it, ensuring
                // a quick retry to re-open this blob for write won't race with the blob closing in the
                // background.
                self.close().await;
                return Err(err);
            }
        }

        Ok(match size {
            0 => TruncateSuccess::Done(self.proxy),
            _ => TruncateSuccess::NeedsData(self.with_state(NeedsData { size, written: 0 })),
        })
    }

    /// Creates a new blob client backed by the returned request stream. This constructor should
    /// not be used outside of tests.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn new_test() -> (Self, fio::FileRequestStream) {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fio::FileMarker>().unwrap();

        (Blob { proxy, state: NeedsTruncate }, stream)
    }
}

impl Blob<NeedsData> {
    /// Writes all of the given buffer to the blob.
    ///
    /// # Panics
    ///
    /// Panics if a write is attempted with a buf larger than the remaining blob size.
    pub async fn write(mut self, buf: &[u8]) -> Result<WriteSuccess, WriteError> {
        assert!(self.state.written + buf.len() as u64 <= self.state.size);

        match fuchsia_fs::file::write(&self.proxy, buf).await {
            Ok(()) => {
                self.state.written += buf.len() as u64;

                if self.state.written == self.state.size {
                    Ok(WriteSuccess::Done(self.with_state(AtEof)))
                } else {
                    Ok(WriteSuccess::MoreToWrite(self))
                }
            }

            Err(e) => {
                self.close().await;

                Err(match e {
                    fuchsia_fs::file::WriteError::Create(_) => {
                        // unreachable!(), but opt to return a confusing error instead of panic.
                        WriteError::UnexpectedResponse(Status::OK)
                    }
                    fuchsia_fs::file::WriteError::Fidl(e) => WriteError::Fidl(e),
                    fuchsia_fs::file::WriteError::WriteError(Status::IO_DATA_INTEGRITY) => {
                        WriteError::Corrupt
                    }
                    fuchsia_fs::file::WriteError::WriteError(Status::NO_SPACE) => {
                        WriteError::NoSpace
                    }
                    fuchsia_fs::file::WriteError::WriteError(status) => {
                        WriteError::UnexpectedResponse(status)
                    }
                    fuchsia_fs::file::WriteError::Overwrite => WriteError::Overwrite,
                })
            }
        }
    }
}

impl Blob<AtEof> {
    /// Rewinds the file position to the start, returning the now read-only FileProxy representing
    /// the blob.
    pub async fn reopen_for_read(self) -> Result<fio::FileProxy, IntoReadError> {
        let _pos: u64 =
            self.proxy.seek(fio::SeekOrigin::Start, 0).await?.map_err(Status::from_raw)?;

        Ok(self.proxy)
    }
}

impl BlobCloser {
    /// Close the blob, silently ignoring errors.
    pub async fn close(mut self) {
        let _ = self.proxy.close().await;
        self.closed = true;
    }

    /// Drops this BlobCloser without closing the underlying blob.
    pub fn disarm(mut self) {
        self.closed = true;
        drop(self);
    }
}

impl Drop for BlobCloser {
    fn drop(&mut self) {
        if !self.closed {
            // Dropped without waiting on close. We can at least send the close request here, but
            // there could be a race with another attempt to open the blob.
            let _ = self.proxy.close();
        }
    }
}

impl TruncateSuccess {
    /// Returns the contained [`TruncateSuccess::NeedsData`] value, consuming the `self` value.
    ///
    /// # Panics
    ///
    /// Panics if the value is a [`TruncateSuccess::Done`].
    pub fn unwrap_needs_data(self) -> Blob<NeedsData> {
        match self {
            TruncateSuccess::NeedsData(blob) => blob,
            _ => panic!("unwrap_needs_data() called on {:?}", self),
        }
    }

    /// Returns the contained [`TruncateSuccess::Done`] value, consuming the `self` value.
    ///
    /// # Panics
    ///
    /// Panics if the value is a [`TruncateSuccess::NeedsData`].
    pub fn unwrap_done(self) -> fio::FileProxy {
        match self {
            TruncateSuccess::Done(blob) => blob,
            _ => panic!("unwrap_done() called on {:?}", self),
        }
    }
}

impl WriteSuccess {
    /// Returns the contained [`WriteSuccess::MoreToWrite`] value, consuming the `self` value.
    ///
    /// # Panics
    ///
    /// Panics if the value is a [`WriteSuccess::Done`].
    pub fn unwrap_more_to_write(self) -> Blob<NeedsData> {
        match self {
            WriteSuccess::MoreToWrite(blob) => blob,
            _ => panic!("unwrap_more_to_write() called on {:?}", self),
        }
    }

    /// Returns the contained [`WriteSuccess::Done`] value, consuming the `self` value.
    ///
    /// # Panics
    ///
    /// Panics if the value is a [`WriteSuccess::MoreToWrite`].
    pub fn unwrap_done(self) -> Blob<AtEof> {
        match self {
            WriteSuccess::Done(blob) => blob,
            _ => panic!("unwrap_done() called on {:?}", self),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::Client,
        assert_matches::assert_matches,
        blobfs_ramdisk::{BlobfsRamdisk, Ramdisk},
        fuchsia_async as fasync,
        fuchsia_merkle::MerkleTree,
        maplit::hashset,
        rand::prelude::*,
    };

    #[fasync::run_singlethreaded(test)]
    async fn empty_blob_is_present_after_truncate() {
        let blobfs = BlobfsRamdisk::start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

        //              _ -  - _
        let contents = [0_0; 0_0];
        let hash = MerkleTree::from_reader(&contents[..]).unwrap().root();

        let blob = client.open_blob_for_write(&hash).await.unwrap();
        let blob = blob.truncate(0u64).await.unwrap().unwrap_done();

        // Verify that:
        // * opening the blob for read has the file_readable signal asserted and is readable
        // * it shows up in readdir
        // * the original proxy is now readable
        assert!(client.has_blob(&hash).await);
        assert_eq!(client.list_known_blobs().await.unwrap(), hashset! {hash});

        let also_blob = client.open_blob_for_read(&hash).await.unwrap();
        assert_eq!(contents, fuchsia_fs::file::read(&also_blob).await.unwrap().as_slice());

        assert_eq!(contents, fuchsia_fs::file::read(&blob).await.unwrap().as_slice());

        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn detects_corrupt_empty_blob() {
        let blobfs = BlobfsRamdisk::start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

        // The empty blob is always named
        // 15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b.
        // A 0 length blob with any other name (like all f's) is corrupt, but also empty.
        let hash = Hash::from([0xff; 32]);

        let blob = client.open_blob_for_write(&hash).await.unwrap();
        assert_matches!(blob.truncate(0u64).await, Err(TruncateError::Corrupt));

        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn detects_corrupt_blob() {
        let blobfs = BlobfsRamdisk::start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

        // The merkle root of b"test" is not all f's, so this blob is corrupt.
        let hash = Hash::from([0xff; 32]);

        let blob = client.open_blob_for_write(&hash).await.unwrap();
        let blob = blob.truncate(4u64).await.unwrap().unwrap_needs_data();
        assert_matches!(blob.write(&b"test"[..]).await, Err(WriteError::Corrupt));

        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_already_present_empty_blob_fails() {
        let blobfs = BlobfsRamdisk::builder().with_blob(&b""[..]).start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

        let hash = MerkleTree::from_reader(&b""[..]).unwrap().root();
        assert_matches!(client.open_blob_for_write(&hash).await, Err(CreateError::AlreadyExists));

        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_already_present_blob_fails() {
        let blobfs = BlobfsRamdisk::builder().with_blob(&b"present"[..]).start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

        let hash = MerkleTree::from_reader(&b"present"[..]).unwrap().root();
        assert_matches!(client.open_blob_for_write(&hash).await, Err(CreateError::AlreadyExists));

        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn write_read_small_blob() {
        let blobfs = BlobfsRamdisk::start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

        let contents = [3; 1024];
        let hash = MerkleTree::from_reader(&contents[..]).unwrap().root();

        let blob = client.open_blob_for_write(&hash).await.unwrap();
        let blob = blob.truncate(1024u64).await.unwrap().unwrap_needs_data();
        let blob = blob.write(&contents[..]).await.unwrap().unwrap_done();

        // New connections can now read the blob, even with the original proxy still open.
        let also_blob = client.open_blob_for_read(&hash).await.unwrap();
        assert_eq!(contents, fuchsia_fs::file::read(&also_blob).await.unwrap().as_slice());

        let blob = blob.reopen_for_read().await.unwrap();

        let actual = fuchsia_fs::file::read(&blob).await.unwrap();
        assert_eq!(contents, actual.as_slice());

        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn write_small_blob_slowly() {
        let blobfs = BlobfsRamdisk::start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

        let contents = [4; 1024];
        let chunk = [4; 1];
        let hash = MerkleTree::from_reader(&contents[..]).unwrap().root();

        let blob = client.open_blob_for_write(&hash).await.unwrap();
        let mut blob = blob.truncate(1024u64).await.unwrap().unwrap_needs_data();
        // verify Blob does correct accounting of written data and forwards small writes correctly
        // by issuing more than 1 write call instead of 1 big call.
        for _ in 0..1023 {
            blob = blob.write(&chunk[..]).await.unwrap().unwrap_more_to_write();
        }
        let blob = blob.write(&chunk[..]).await.unwrap().unwrap_done();

        let blob = blob.reopen_for_read().await.unwrap();

        let actual = fuchsia_fs::file::read(&blob).await.unwrap();
        assert_eq!(contents, actual.as_slice());

        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn write_large_blob() {
        let blobfs = BlobfsRamdisk::start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

        let contents = (0u8..=255u8).cycle().take(1_000_000).collect::<Vec<u8>>();
        let hash = MerkleTree::from_reader(&contents[..]).unwrap().root();

        let blob = client.open_blob_for_write(&hash).await.unwrap();
        let blob = blob.truncate(contents.len() as u64).await.unwrap().unwrap_needs_data();
        let blob = blob.write(&contents[..]).await.unwrap().unwrap_done();

        let blob = blob.reopen_for_read().await.unwrap();

        let actual = fuchsia_fs::file::read(&blob).await.unwrap();
        assert_eq!(contents, actual.as_slice());

        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn close_blob_closer() {
        let blobfs = BlobfsRamdisk::start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

        let contents = [3; 1024];
        let hash = MerkleTree::from_reader(&contents[..]).unwrap().root();

        let blob = client.open_blob_for_write(&hash).await.unwrap();
        // make a closer and then immediately close it, then use the now closed blob proxy to show
        // that it has been closed.
        blob.closer().close().await;
        assert_matches!(
            blob.truncate(1024u64).await,
            Err(TruncateError::Fidl(fidl::Error::ClientChannelClosed {
                status: Status::PEER_CLOSED,
                ..
            }))
        );

        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn disarm_blob_closer() {
        let blobfs = BlobfsRamdisk::start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

        let contents = [3; 1024];
        let hash = MerkleTree::from_reader(&contents[..]).unwrap().root();

        let blob = client.open_blob_for_write(&hash).await.unwrap();
        // make and disarm a closer, then use the not-closed blob proxy to show it is still open.
        blob.closer().disarm();
        let blob = blob.truncate(1024u64).await.unwrap().unwrap_needs_data();
        let blob = blob.write(&contents[..]).await.unwrap().unwrap_done();
        drop(blob);

        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn concurrent_write_at_truncate() {
        let blobfs = BlobfsRamdisk::start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

        let contents = [3; 1024];
        let hash = MerkleTree::from_reader(&contents[..]).unwrap().root();

        let blob1 = client.open_blob_for_write(&hash).await.unwrap();
        let _blob1 = blob1.truncate(1024u64).await.unwrap().unwrap_needs_data();

        assert_matches!(client.open_blob_for_write(&hash).await, Err(CreateError::AlreadyExists));

        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn concurrent_write_at_create() {
        let blobfs = BlobfsRamdisk::start().unwrap();
        let client = Client::for_ramdisk(&blobfs);

        let contents = [3; 1024];
        let hash = MerkleTree::from_reader(&contents[..]).unwrap().root();

        // concurrent opens will succeed as long as no connections have called truncate() yet.
        // First truncate() wins.
        let blob1 = client.open_blob_for_write(&hash).await.unwrap();
        let blob2 = client.open_blob_for_write(&hash).await.unwrap();
        let _blob1 = blob1.truncate(1024u64).await.unwrap().unwrap_needs_data();
        assert_matches!(blob2.truncate(1024u64).await, Err(TruncateError::ConcurrentWrite));

        blobfs.stop().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn write_too_big_blob_fails_with_no_space() {
        let tiny_blobfs =
            Ramdisk::builder().block_count(4096).into_blobfs_builder().unwrap().start().unwrap();
        let client = Client::for_ramdisk(&tiny_blobfs);

        // Deterministically generate a blob that cannot be compressed and is bigger than blobfs
        // can store.
        const LARGE_BLOB_FILE_SIZE: u64 = 4 * 1024 * 1024;
        let mut contents = vec![0u8; LARGE_BLOB_FILE_SIZE as usize];
        let mut rng = StdRng::from_seed([0u8; 32]);
        rng.fill_bytes(&mut contents[..]);
        let hash = MerkleTree::from_reader(&contents[..]).unwrap().root();

        // Verify writing that blob fails with an out of space error.
        let blob = client.open_blob_for_write(&hash).await.unwrap();
        let blob = blob.truncate(LARGE_BLOB_FILE_SIZE).await.unwrap().unwrap_needs_data();
        assert_matches!(blob.write(&contents[..]).await, Err(WriteError::NoSpace));

        tiny_blobfs.stop().await.unwrap();
    }
}
