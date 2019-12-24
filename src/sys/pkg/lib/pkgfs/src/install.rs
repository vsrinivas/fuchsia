// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around the /pkgfs/install filesystem.

use {
    crate::iou,
    fidl_fuchsia_io::{DirectoryProxy, FileProxy},
    fuchsia_merkle::Hash,
    fuchsia_zircon::Status,
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
    Io(iou::OpenError),
}

/// An open handle to /pkgfs/install
#[derive(Debug, Clone)]
pub struct Client {
    proxy: DirectoryProxy,
}

impl Client {
    /// Returns an client connected to pkgfs from the current component's namespace
    pub fn open_from_namespace() -> Result<Self, anyhow::Error> {
        let proxy = iou::open_directory_from_namespace("/pkgfs/install")?;
        Ok(Client { proxy })
    }

    /// Returns an client connected to pkgfs from the given pkgfs root dir.
    pub fn open_from_pkgfs_root(pkgfs: &DirectoryProxy) -> Result<Self, anyhow::Error> {
        Ok(Client {
            proxy: iou::open_directory_no_describe(
                pkgfs,
                "install",
                fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
            )?,
        })
    }

    /// Create a new blob with the given install intent. Returns an open file proxy to the blob.
    pub async fn create_blob(
        &self,
        merkle: Hash,
        blob_kind: BlobKind,
    ) -> Result<(Blob<NeedsTruncate>, BlobCloser), BlobCreateError> {
        let flags = fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE;

        let blob = iou::open_file(&self.proxy, &blob_kind.make_install_path(&merkle), flags)
            .await
            .map_err(|e| match e {
                iou::OpenError::OpenError(Status::ALREADY_EXISTS) => {
                    // Lost a race writing to blobfs, and the blob already exists.
                    BlobCreateError::AlreadyExists
                }
                iou::OpenError::OpenError(Status::ACCESS_DENIED) => {
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

/// An error encountered while truncating a blob
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum BlobTruncateError {
    #[error("fidl error: {}", _0)]
    Fidl(fidl::Error),

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

        Status::ok(s).map_err(BlobTruncateError::UnexpectedResponse)?;

        Ok(Blob { proxy: self.proxy, kind: self.kind, state: NeedsData { size, written: 0 } })
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
        let fut = self.proxy.write(&mut buf.iter().cloned());

        let (status, actual) = fut.await.map_err(BlobWriteError::Fidl)?;

        match Status::from_raw(status) {
            Status::OK => {}
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
        assert_matches!(
            blob.write("bar".as_bytes()).await,
            Err(BlobWriteError::UnexpectedResponse(Status::IO_DATA_INTEGRITY))
        );
        closer.close().await;

        // Retrying with the correct data succeeds.
        client.write_blob(pkg_contents["data/corrupt"], BlobKind::Data, "foo".as_bytes()).await;

        pkgfs.stop().await.unwrap();
    }
}
