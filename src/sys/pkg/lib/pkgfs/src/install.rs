// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around the /pkgfs/install filesystem.

use {
    fidl::endpoints::RequestStream,
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, DirectoryRequest, DirectoryRequestStream, FileMarker,
        FileObject, FileProxy, FileRequest, FileRequestStream, NodeInfo,
    },
    fuchsia_hash::Hash,
    fuchsia_zircon::Status,
    futures::prelude::*,
    thiserror::Error,
};

/// The kind of blob to be installed.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum BlobKind {
    /// The blob should be interpreted as a package.
    Package,

    /// The blob should be interpreted as a content blob in a package.
    Data,
}

impl BlobKind {
    fn make_install_path(&self, merkle: &Hash) -> String {
        let name = match *self {
            BlobKind::Package => "pkg",
            BlobKind::Data => "blob",
        };
        format!("{}/{}", name, merkle)
    }
}

/// An error encountered while creating a blob
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum BlobCreateError {
    #[error("the blob already exists and is readable")]
    AlreadyExists,

    #[error("the blob is in the process of being written")]
    ConcurrentWrite,

    #[error("while creating the blob: {}", _0)]
    Io(io_util::node::OpenError),
}

/// An open handle to /pkgfs/install
#[derive(Debug, Clone)]
pub struct Client {
    proxy: DirectoryProxy,
}

impl Client {
    /// Returns an client connected to pkgfs from the current component's namespace
    pub fn open_from_namespace() -> Result<Self, io_util::node::OpenError> {
        let proxy = io_util::directory::open_in_namespace(
            "/pkgfs/install",
            fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )?;
        Ok(Client { proxy })
    }

    /// Returns an client connected to pkgfs from the given pkgfs root dir.
    pub fn open_from_pkgfs_root(pkgfs: &DirectoryProxy) -> Result<Self, io_util::node::OpenError> {
        Ok(Client {
            proxy: io_util::directory::open_directory_no_describe(
                pkgfs,
                "install",
                fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
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

    /// Create a new blob with the given install intent. Returns an open file proxy to the blob.
    pub async fn create_blob(
        &self,
        merkle: Hash,
        blob_kind: BlobKind,
    ) -> Result<(Blob<NeedsTruncate>, BlobCloser), BlobCreateError> {
        let flags = fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE;

        let blob = io_util::directory::open_file(
            &self.proxy,
            &blob_kind.make_install_path(&merkle),
            flags,
        )
        .await
        .map_err(|e| match e {
            io_util::node::OpenError::OpenError(Status::ALREADY_EXISTS) => {
                // Lost a race writing to blobfs, and the blob already exists.
                BlobCreateError::AlreadyExists
            }
            io_util::node::OpenError::OpenError(Status::ACCESS_DENIED) => {
                // Lost a race with another process writing to blobfs, and the blob is in the
                // process of being written.
                BlobCreateError::ConcurrentWrite
            }
            other => BlobCreateError::Io(other),
        })?;

        Ok((
            Blob { proxy: Clone::clone(&blob), kind: blob_kind, state: NeedsTruncate },
            BlobCloser { proxy: blob, closed: false },
        ))
    }
}

/// A testing server implementation of /pkgfs/install.
///
/// Mock does not handle requests until instructed to do so.
pub struct Mock {
    stream: DirectoryRequestStream,
}

impl Mock {
    /// Consume the next directory request, verifying it is intended to create the blob identified
    /// by `merkle` and `kind`.  Returns a `MockBlob` representing the open blob install file.
    ///
    /// # Panics
    ///
    /// Panics on error or assertion violation (unexpected requests or a mismatched open call)
    pub async fn expect_create_blob(&mut self, merkle: Hash, kind: BlobKind) -> MockBlob {
        match self.stream.next().await {
            Some(Ok(DirectoryRequest::Open {
                flags: _,
                mode: _,
                path,
                object,
                control_handle: _,
            })) => {
                assert_eq!(path, kind.make_install_path(&merkle));

                let stream = object.into_stream().unwrap().cast_stream();
                MockBlob { stream }
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

/// A testing server implementation of an open /pkgfs/install/{pkg,blob}/<merkle> file.
///
/// MockBlob does not send the OnOpen event or handle requests until instructed to do so.
pub struct MockBlob {
    stream: FileRequestStream,
}

impl MockBlob {
    fn send_on_open(&mut self, status: Status) {
        let mut info = NodeInfo::File(FileObject { event: None, stream: None });
        let () =
            self.stream.control_handle().send_on_open_(status.into_raw(), Some(&mut info)).unwrap();
    }

    async fn handle_truncate(&mut self, status: Status) -> u64 {
        match self.stream.next().await {
            Some(Ok(FileRequest::Truncate { length, responder })) => {
                responder.send(status.into_raw()).unwrap();

                length
            }
            other => panic!("unexpected request: {:?}", other),
        }
    }

    async fn expect_truncate(&mut self) -> u64 {
        self.handle_truncate(Status::OK).await
    }

    async fn handle_write(&mut self, status: Status) -> Vec<u8> {
        match self.stream.next().await {
            Some(Ok(FileRequest::Write { data, responder })) => {
                responder.send(status.into_raw(), data.len() as u64).unwrap();

                data
            }
            other => panic!("unexpected request: {:?}", other),
        }
    }

    fn fail_open_with_error(mut self, status: Status) {
        assert_ne!(status, Status::OK);
        self.send_on_open(status);
    }

    /// Fail the open request with an error indicating the blob already exists.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn fail_open_with_already_exists(self) {
        self.fail_open_with_error(Status::ALREADY_EXISTS);
    }

    /// Fail the open request with an error indicating the blob is open for concurrent write.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn fail_open_with_concurrent_write(self) {
        self.fail_open_with_error(Status::ACCESS_DENIED);
    }

    async fn fail_write_with_status(mut self, status: Status) {
        self.send_on_open(Status::OK);

        let length = self.expect_truncate().await;
        // divide rounding up
        let expected_write_calls =
            (length + (fidl_fuchsia_io::MAX_BUF - 1)) / fidl_fuchsia_io::MAX_BUF;
        for _ in 0..(expected_write_calls - 1) {
            self.handle_write(Status::OK).await;
        }
        self.handle_write(status).await;
    }

    /// Succeeds the open request, consumes the truncate request, the initial write calls, then
    /// fails the final write indicating the written data was corrupt.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub async fn fail_write_with_corrupt(self) {
        self.fail_write_with_status(Status::IO_DATA_INTEGRITY).await
    }

    /// Succeeds the open request, then verifies the blob is truncated, written, and closed with
    /// the given `expected` payload.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub async fn expect_payload(mut self, expected: &[u8]) {
        self.send_on_open(Status::OK);

        assert_eq!(self.expect_truncate().await, expected.len() as u64);

        let mut rest = expected;
        while !rest.is_empty() {
            let expected_chunk = if rest.len() > fidl_fuchsia_io::MAX_BUF as usize {
                &rest[..fidl_fuchsia_io::MAX_BUF as usize]
            } else {
                rest
            };
            assert_eq!(self.handle_write(Status::OK).await, expected_chunk);
            rest = &rest[expected_chunk.len()..];
        }

        match self.stream.next().await {
            Some(Ok(FileRequest::Close { responder })) => {
                responder.send(Status::OK.into_raw()).unwrap();
            }
            other => panic!("unexpected request: {:?}", other),
        }
    }
}

/// An error encountered while truncating a blob
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum BlobTruncateError {
    #[error("fidl error: {}", _0)]
    Fidl(fidl::Error),

    #[error("fidl endpoint reported that insufficient storage space is available")]
    NoSpace,

    #[error("received unexpected failure status: {}", _0)]
    UnexpectedResponse(Status),
}

/// An error encountered while truncating a blob
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum BlobWriteError {
    #[error("fidl error: {}", _0)]
    Fidl(fidl::Error),

    #[error("file endpoint reported more bytes written than were provided")]
    Overwrite,

    #[error("fidl endpoint reported the written data was corrupt")]
    Corrupt,

    #[error("fidl endpoint reported that insufficient storage space is available")]
    NoSpace,

    #[error("received unexpected failure status: {}", _0)]
    UnexpectedResponse(Status),
}

/// A handle to a blob that must be explicitly closed to prevent future opens of the same blob from
/// racing with this blob closing.
#[derive(Debug)]
#[must_use]
pub struct BlobCloser {
    proxy: FileProxy,
    closed: bool,
}

impl BlobCloser {
    /// Close the blob, silently ignoring errors.
    pub async fn close(mut self) {
        let _ = self.proxy.close().await;
        self.closed = true;
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

/// The successful result of writing some data to a blob.
#[derive(Debug)]
pub enum BlobWriteSuccess {
    /// There is still more data to write.
    MoreToWrite(Blob<NeedsData>),

    /// The blob is fully written.
    Done,
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

/// A blob in the process of being written.
#[derive(Debug)]
pub struct Blob<S> {
    proxy: FileProxy,
    kind: BlobKind,
    state: S,
}

impl Blob<NeedsTruncate> {
    /// Truncates the blob to the given size. On success, the blob enters the writable state.
    pub async fn truncate(self, size: u64) -> Result<Blob<NeedsData>, BlobTruncateError> {
        let s = self.proxy.truncate(size).await.map_err(BlobTruncateError::Fidl)?;

        match Status::from_raw(s) {
            Status::OK => {}
            Status::NO_SPACE => return Err(BlobTruncateError::NoSpace),
            status => return Err(BlobTruncateError::UnexpectedResponse(status)),
        }

        Ok(Blob { proxy: self.proxy, kind: self.kind, state: NeedsData { size, written: 0 } })
    }

    /// Creates a new blob/closer client backed by the returned request stream. This constructor
    /// should not be used outside of tests.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn new_test(kind: BlobKind) -> (Self, BlobCloser, FileRequestStream) {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<FileMarker>().unwrap();

        (
            Blob { proxy: Clone::clone(&proxy), kind, state: NeedsTruncate },
            BlobCloser { proxy, closed: false },
            stream,
        )
    }
}

impl Blob<NeedsData> {
    /// Writes all of the given buffer to the blob.
    ///
    /// # Panics
    ///
    /// Panics if a write is attempted with a buf larger than the remaining blob size.
    pub async fn write(mut self, mut buf: &[u8]) -> Result<BlobWriteSuccess, BlobWriteError> {
        assert!(self.state.written + buf.len() as u64 <= self.state.size);

        while !buf.is_empty() {
            // Don't try to write more than MAX_BUF bytes at a time.
            let limit = buf.len().min(fidl_fuchsia_io::MAX_BUF as usize);

            let written = self.write_some(&buf[..limit]).await?;

            buf = &buf[written..];
        }

        if self.state.written == self.state.size {
            Ok(BlobWriteSuccess::Done)
        } else {
            Ok(BlobWriteSuccess::MoreToWrite(self))
        }
    }

    /// Writes some of the given buffer to the blob.
    ///
    /// Returns the number of bytes written (which may be less than the buffer's size) or the error
    /// encountered during the write.
    async fn write_some(&mut self, buf: &[u8]) -> Result<usize, BlobWriteError> {
        let fut = self.proxy.write(buf);

        let (status, actual) = fut.await.map_err(BlobWriteError::Fidl)?;

        match Status::from_raw(status) {
            Status::OK => {}
            Status::IO_DATA_INTEGRITY => {
                return Err(BlobWriteError::Corrupt);
            }
            Status::NO_SPACE => {
                return Err(BlobWriteError::NoSpace);
            }
            Status::ALREADY_EXISTS => {
                if self.kind == BlobKind::Package && self.state.written + actual == self.state.size
                {
                    // pkgfs returns ALREADY_EXISTS on the final write of a meta FAR iff no other
                    // needs exist. Allow the error, but ignore the hint and check needs anyway.
                } else {
                    return Err(BlobWriteError::UnexpectedResponse(Status::ALREADY_EXISTS));
                }
            }
            status => {
                return Err(BlobWriteError::UnexpectedResponse(status));
            }
        }

        if actual > buf.len() as u64 {
            return Err(BlobWriteError::Overwrite);
        }

        self.state.written += actual;
        Ok(actual as usize)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_merkle::MerkleTree,
        fuchsia_pkg_testing::{Package, PackageBuilder},
        matches::assert_matches,
        pkgfs_ramdisk::PkgfsRamdisk,
        std::io::Read,
    };

    impl Client {
        pub(crate) async fn write_blob(&self, merkle: Hash, blob_kind: BlobKind, data: &[u8]) {
            let (blob, closer) = self.create_blob(merkle, blob_kind).await.unwrap();
            let blob = blob.truncate(data.len() as u64).await.unwrap();
            assert_matches!(blob.write(data).await, Ok(BlobWriteSuccess::Done));
            closer.close().await;
        }

        pub(crate) async fn write_meta_far(&self, pkg: &Package) {
            let mut buf = vec![];
            pkg.meta_far().unwrap().read_to_end(&mut buf).unwrap();
            let pkg_merkle = pkg.meta_far_merkle_root().to_owned();

            self.write_blob(pkg_merkle, BlobKind::Package, &buf[..]).await;
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handles_empty_content_blob() {
        let pkgfs = PkgfsRamdisk::start().unwrap();
        let root = pkgfs.root_dir_proxy().unwrap();
        let client = Client::open_from_pkgfs_root(&root).unwrap();

        // Make the test package.
        let pkg = PackageBuilder::new("handles-empty-content-blob")
            .add_resource_at("data/empty", "".as_bytes())
            .build()
            .await
            .unwrap();

        // Write the meta far.
        client.write_meta_far(&pkg).await;

        // Write the empty blob.
        let empty_merkle = MerkleTree::from_reader(std::io::empty()).unwrap().root();
        client.write_blob(empty_merkle, BlobKind::Data, &[]).await;

        pkgfs.stop().await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn prevents_duplicate_blob_writes() {
        let pkgfs = PkgfsRamdisk::start().unwrap();
        let root = pkgfs.root_dir_proxy().unwrap();
        let client = Client::open_from_pkgfs_root(&root).unwrap();

        // Make the test package.
        let pkg = PackageBuilder::new("prevents-duplicate-blob-writes")
            .add_resource_at("data/write-twice", "will it detect the race?".as_bytes())
            .add_resource_at("data/prevent-activation", "by not writing this blob".as_bytes())
            .build()
            .await
            .unwrap();
        let pkg_contents = pkg.meta_contents().unwrap().contents().to_owned();

        // Write the meta far.
        client.write_meta_far(&pkg).await;

        // Write the test blob the first time.
        client
            .write_blob(
                pkg_contents["data/write-twice"],
                BlobKind::Data,
                "will it detect the race?".as_bytes(),
            )
            .await;

        // Pkgfs fails repeat attempts to write the same blob.
        assert_matches!(
            client.create_blob(pkg_contents["data/write-twice"], BlobKind::Data).await,
            Err(BlobCreateError::AlreadyExists)
        );

        pkgfs.stop().await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn rejects_corrupt_content_blobs() {
        let pkgfs = PkgfsRamdisk::start().unwrap();
        let root = pkgfs.root_dir_proxy().unwrap();
        let client = Client::open_from_pkgfs_root(&root).unwrap();

        // Make the test package.
        let pkg = PackageBuilder::new("rejects-corrupt-content-blob")
            .add_resource_at("data/corrupt", "foo".as_bytes())
            .build()
            .await
            .unwrap();
        let pkg_contents = pkg.meta_contents().unwrap().contents().to_owned();

        // Write the meta far.
        client.write_meta_far(&pkg).await;

        // Writing invalid blob data fails.
        let (blob, closer) =
            client.create_blob(pkg_contents["data/corrupt"], BlobKind::Data).await.unwrap();
        let blob = blob.truncate("foo".len() as u64).await.unwrap();
        assert_matches!(blob.write("bar".as_bytes()).await, Err(BlobWriteError::Corrupt));
        closer.close().await;

        // Retrying with the correct data succeeds.
        client.write_blob(pkg_contents["data/corrupt"], BlobKind::Data, "foo".as_bytes()).await;

        pkgfs.stop().await.unwrap();
    }
}
