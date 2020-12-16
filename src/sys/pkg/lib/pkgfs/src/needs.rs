// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around the /pkgfs/needs filesystem.

use {
    fidl::endpoints::RequestStream,
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, DirectoryRequest, DirectoryRequestStream, NodeInfo,
    },
    fuchsia_hash::{Hash, ParseHashError},
    fuchsia_zircon::Status,
    futures::prelude::*,
    std::{
        collections::{BTreeSet, HashSet},
        convert::TryInto,
    },
    thiserror::Error,
};

#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum ListNeedsError {
    #[error("while opening needs dir: {}", _0)]
    OpenDir(io_util::node::OpenError),

    #[error("while listing needs dir: {}", _0)]
    ReadDir(files_async::Error),

    #[error("unable to parse a need blob id: {}", _0)]
    ParseError(ParseHashError),
}

/// An open handle to /pkgfs/needs
#[derive(Debug, Clone)]
pub struct Client {
    proxy: DirectoryProxy,
}

impl Client {
    /// Returns an client connected to pkgfs from the current component's namespace
    pub fn open_from_namespace() -> Result<Self, io_util::node::OpenError> {
        let proxy = io_util::directory::open_in_namespace(
            "/pkgfs/needs",
            fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        )?;
        Ok(Client { proxy })
    }

    /// Returns an client connected to pkgfs from the given pkgfs root dir.
    pub fn open_from_pkgfs_root(pkgfs: &DirectoryProxy) -> Result<Self, io_util::node::OpenError> {
        Ok(Client {
            proxy: io_util::directory::open_directory_no_describe(
                pkgfs,
                "needs",
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

    /// Returns a stream of chunks of blobs that are needed to resolve the package specified by
    /// `pkg_merkle` provided that the `pkg_merkle` blob has previously been written to
    /// /pkgfs/install/pkg/. The package should be available in /pkgfs/versions when this stream
    /// terminates without error.
    pub fn list_needs(
        &self,
        pkg_merkle: Hash,
    ) -> impl Stream<Item = Result<HashSet<Hash>, ListNeedsError>> + '_ {
        // None if stream is terminated and should not continue to enumerate needs.
        let state = Some(&self.proxy);

        futures::stream::unfold(state, move |state: Option<&DirectoryProxy>| {
            async move {
                if let Some(proxy) = state {
                    match enumerate_needs_dir(proxy, pkg_merkle).await {
                        Ok(needs) => {
                            if needs.is_empty() {
                                None
                            } else {
                                Some((Ok(needs), Some(proxy)))
                            }
                        }
                        // report the error and terminate the stream.
                        Err(err) => return Some((Err(err), None)),
                    }
                } else {
                    None
                }
            }
        })
    }
}

/// Lists all blobs currently in the `pkg_merkle`'s needs directory.
async fn enumerate_needs_dir(
    pkgfs_needs: &DirectoryProxy,
    pkg_merkle: Hash,
) -> Result<HashSet<Hash>, ListNeedsError> {
    let path = format!("packages/{}", pkg_merkle);
    let flags = fidl_fuchsia_io::OPEN_RIGHT_READABLE;

    let needs_dir = match io_util::directory::open_directory(pkgfs_needs, &path, flags).await {
        Ok(dir) => dir,
        Err(io_util::node::OpenError::OpenError(Status::NOT_FOUND)) => return Ok(HashSet::new()),
        Err(e) => return Err(ListNeedsError::OpenDir(e)),
    };

    let entries = files_async::readdir(&needs_dir).await.map_err(ListNeedsError::ReadDir)?;

    Ok(entries
        .into_iter()
        .filter_map(|entry| {
            if entry.kind == files_async::DirentKind::File {
                Some(entry.name.parse().map_err(ListNeedsError::ParseError))
            } else {
                // Ignore unknown entries.
                None
            }
        })
        .collect::<Result<HashSet<Hash>, ListNeedsError>>()?)
}

/// A testing server implementation of /pkgfs/needs.
///
/// Mock does not handle requests until instructed to do so.
pub struct Mock {
    stream: DirectoryRequestStream,
}

impl Mock {
    /// Consume the next directory request, verifying it is intended to open the directory
    /// containing the needs for the given package meta far `merkle`.  Returns a `MockNeeds`
    /// representing the open needs directory.
    ///
    /// # Panics
    ///
    /// Panics on error or assertion violation (unexpected requests or a mismatched open call)
    pub async fn expect_enumerate_needs(&mut self, merkle: Hash) -> MockNeeds {
        match self.stream.next().await {
            Some(Ok(DirectoryRequest::Open {
                flags: _,
                mode: _,
                path,
                object,
                control_handle: _,
            })) => {
                assert_eq!(path, format!("packages/{}", merkle));

                let stream = object.into_stream().unwrap().cast_stream();
                MockNeeds { stream }
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

/// A testing server implementation of an open /pkgfs/needs/packages/<merkle> directory.
///
/// MockNeeds does not send the OnOpen event or handle requests until instructed to do so.
pub struct MockNeeds {
    stream: DirectoryRequestStream,
}

impl MockNeeds {
    fn send_on_open(&mut self, status: Status) {
        let mut info = NodeInfo::Directory(fidl_fuchsia_io::DirectoryObject);
        let () =
            self.stream.control_handle().send_on_open_(status.into_raw(), Some(&mut info)).unwrap();
    }

    async fn handle_rewind(&mut self) {
        match self.stream.next().await {
            Some(Ok(DirectoryRequest::Rewind { responder })) => {
                responder.send(Status::OK.into_raw()).unwrap();
            }
            other => panic!("unexpected request: {:?}", other),
        }
    }

    /// Fail the open request with an error indicating there are no needs.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub async fn fail_open_with_not_found(mut self) {
        self.send_on_open(Status::NOT_FOUND);
    }

    /// Fail the open request with an unexpected error status.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub async fn fail_open_with_unexpected_error(mut self) {
        self.send_on_open(Status::INTERNAL);
    }

    /// Succeeds the open request, then handles incoming read_dirents requests to provide the
    /// client with the given `needs`.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub async fn enumerate_needs(mut self, needs: BTreeSet<Hash>) {
        self.send_on_open(Status::OK);

        // files_async starts by resetting the directory channel's readdir position.
        self.handle_rewind().await;

        #[repr(C, packed)]
        struct Dirent {
            ino: u64,
            size: u8,
            kind: u8,
            name: [u8; 64],
        }

        impl Dirent {
            fn as_bytes(&self) -> &[u8] {
                let start = self as *const Self as *const u8;
                // Safe because the FIDL wire format for directory entries is
                // defined to be the C packed struct representation used here.
                unsafe { std::slice::from_raw_parts(start, std::mem::size_of::<Self>()) }
            }
        }

        let mut needs_iter = needs.iter().enumerate().map(|(i, hash)| Dirent {
            ino: i as u64 + 1,
            size: 64,
            kind: fidl_fuchsia_io::DIRENT_TYPE_FILE,
            name: hash.to_string().as_bytes().try_into().unwrap(),
        });

        while let Some(request) = self.stream.try_next().await.unwrap() {
            match request {
                DirectoryRequest::ReadDirents { max_bytes, responder } => {
                    let max_bytes = max_bytes as usize;
                    assert!(max_bytes >= std::mem::size_of::<Dirent>());

                    let mut buf = vec![];
                    while buf.len() + std::mem::size_of::<Dirent>() <= max_bytes {
                        match needs_iter.next() {
                            Some(need) => {
                                buf.extend(need.as_bytes());
                            }
                            None => break,
                        }
                    }

                    responder.send(Status::OK.into_raw(), &buf).unwrap();
                }
                other => panic!("unexpected request: {:?}", other),
            }
        }

        assert!(matches!(needs_iter.next(), None));
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::install::{BlobCreateError, BlobKind, BlobWriteSuccess},
        fuchsia_hash::HashRangeFull,
        fuchsia_pkg_testing::PackageBuilder,
        maplit::hashset,
        matches::assert_matches,
        pkgfs_ramdisk::PkgfsRamdisk,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn no_needs_is_empty_needs() {
        let pkgfs = PkgfsRamdisk::start().unwrap();
        let root = pkgfs.root_dir_proxy().unwrap();
        let client = Client::open_from_pkgfs_root(&root).unwrap();

        let merkle = fuchsia_merkle::MerkleTree::from_reader(std::io::empty()).unwrap().root();
        let mut needs = client.list_needs(merkle).boxed();
        assert_matches!(needs.next().await, None);

        pkgfs.stop().await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn list_needs() {
        let pkgfs = PkgfsRamdisk::start().unwrap();
        let root = pkgfs.root_dir_proxy().unwrap();
        let install = crate::install::Client::open_from_pkgfs_root(&root).unwrap();
        let client = Client::open_from_pkgfs_root(&root).unwrap();

        let pkg = PackageBuilder::new("list-needs")
            .add_resource_at("data/blob1", "blob1".as_bytes())
            .add_resource_at("data/blob2", "blob2".as_bytes())
            .build()
            .await
            .unwrap();
        let pkg_contents = pkg.meta_contents().unwrap().contents().to_owned();

        install.write_meta_far(&pkg).await;

        let mut needs = client.list_needs(pkg.meta_far_merkle_root().to_owned()).boxed();
        assert_matches!(
            needs.next().await,
            Some(Ok(needs)) if needs == hashset! {
                pkg_contents["data/blob1"],
                pkg_contents["data/blob2"],
            }
        );

        install.write_blob(pkg_contents["data/blob1"], BlobKind::Data, "blob1".as_bytes()).await;
        assert_matches!(
            needs.next().await,
            Some(Ok(needs)) if needs == hashset! {
                pkg_contents["data/blob2"],
            }
        );

        install.write_blob(pkg_contents["data/blob2"], BlobKind::Data, "blob2".as_bytes()).await;
        assert_matches!(needs.next().await, None);

        pkgfs.stop().await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn shared_blob_still_needed_while_being_written() {
        let pkgfs = PkgfsRamdisk::start().unwrap();
        let root = pkgfs.root_dir_proxy().unwrap();
        let install = crate::install::Client::open_from_pkgfs_root(&root).unwrap();
        let versions = crate::versions::Client::open_from_pkgfs_root(&root).unwrap();
        let client = Client::open_from_pkgfs_root(&root).unwrap();

        const SHARED_BLOB_CONTENTS: &[u8] = "shared between both packages".as_bytes();

        let pkg1 = PackageBuilder::new("shared-content-a")
            .add_resource_at("data/shared", SHARED_BLOB_CONTENTS)
            .build()
            .await
            .unwrap();
        let pkg2 = PackageBuilder::new("shared-content-b")
            .add_resource_at("data/shared", SHARED_BLOB_CONTENTS)
            .build()
            .await
            .unwrap();
        let pkg_contents = pkg1.meta_contents().unwrap().contents().to_owned();

        install.write_meta_far(&pkg1).await;

        // start writing the shared blob, but don't finish.
        let (blob, closer) =
            install.create_blob(pkg_contents["data/shared"], BlobKind::Data).await.unwrap();
        let blob = blob.truncate(SHARED_BLOB_CONTENTS.len() as u64).await.unwrap();
        let (first, second) = SHARED_BLOB_CONTENTS.split_at(10);
        let blob = match blob.write(first).await.unwrap() {
            BlobWriteSuccess::MoreToWrite(blob) => blob,
            BlobWriteSuccess::Done => unreachable!(),
        };

        // start installing the second package, and verify the shared blob is listed in needs
        install.write_meta_far(&pkg2).await;
        let mut needs = client.list_needs(pkg2.meta_far_merkle_root().to_owned()).boxed();
        assert_matches!(
            needs.next().await,
            Some(Ok(needs)) if needs == hashset! {
                pkg_contents["data/shared"],
            }
        );

        // finish writing the shared blob, and verify both packages are now complete.
        assert_matches!(blob.write(second).await, Ok(BlobWriteSuccess::Done));
        closer.close().await;

        assert_matches!(needs.next().await, None);

        let pkg1_dir =
            versions.open_package(pkg1.meta_far_merkle_root()).await.unwrap().into_proxy();
        pkg1.verify_contents(&pkg1_dir).await.unwrap();

        let pkg2_dir =
            versions.open_package(pkg2.meta_far_merkle_root()).await.unwrap().into_proxy();
        pkg2.verify_contents(&pkg2_dir).await.unwrap();

        pkgfs.stop().await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn initially_present_blobs_are_not_needed() {
        let pkgfs = PkgfsRamdisk::start().unwrap();
        let root = pkgfs.root_dir_proxy().unwrap();
        let install = crate::install::Client::open_from_pkgfs_root(&root).unwrap();
        let client = Client::open_from_pkgfs_root(&root).unwrap();

        const PRESENT_BLOB_CONTENTS: &[u8] = "already here".as_bytes();

        let pkg = PackageBuilder::new("partially-cached")
            .add_resource_at("data/present", PRESENT_BLOB_CONTENTS)
            .add_resource_at("data/needed", "need to fetch this one".as_bytes())
            .build()
            .await
            .unwrap();
        let pkg_contents = pkg.meta_contents().unwrap().contents().to_owned();

        // write the present blob and start installing the package.
        pkgfs
            .blobfs()
            .add_blob_from(
                &fuchsia_merkle::MerkleTree::from_reader(PRESENT_BLOB_CONTENTS).unwrap().root(),
                PRESENT_BLOB_CONTENTS,
            )
            .unwrap();
        install.write_meta_far(&pkg).await;

        // confirm that the needed blob is needed and the present blob is present.
        let mut needs = client.list_needs(pkg.meta_far_merkle_root().to_owned()).boxed();
        assert_matches!(
            needs.next().await,
            Some(Ok(needs)) if needs == hashset! {
                pkg_contents["data/needed"],
            }
        );

        pkgfs.stop().await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn racing_blob_writes_do_not_fulfill_partial_blobs() {
        let pkgfs = PkgfsRamdisk::start().unwrap();
        let root = pkgfs.root_dir_proxy().unwrap();
        let install = crate::install::Client::open_from_pkgfs_root(&root).unwrap();
        let versions = crate::versions::Client::open_from_pkgfs_root(&root).unwrap();
        let client = Client::open_from_pkgfs_root(&root).unwrap();

        const REQUIRED_BLOB_CONTENTS: &[u8] = "don't fulfill me early please".as_bytes();

        let pkg = PackageBuilder::new("partially-cached")
            .add_resource_at("data/required", REQUIRED_BLOB_CONTENTS)
            .build()
            .await
            .unwrap();
        let pkg_contents = pkg.meta_contents().unwrap().contents().to_owned();

        // write the package meta far and verify the needed blob is needed.
        install.write_meta_far(&pkg).await;
        let mut needs = client.list_needs(pkg.meta_far_merkle_root().to_owned()).boxed();
        assert_matches!(
            needs.next().await,
            Some(Ok(needs)) if needs == hashset! {
                pkg_contents["data/required"],
            }
        );

        // start writing the content blob, but don't finish yet.
        let (blob, closer) =
            install.create_blob(pkg_contents["data/required"], BlobKind::Data).await.unwrap();
        let blob = blob.truncate(REQUIRED_BLOB_CONTENTS.len() as u64).await.unwrap();
        let blob = match blob.write("don't ".as_bytes()).await.unwrap() {
            BlobWriteSuccess::MoreToWrite(blob) => blob,
            BlobWriteSuccess::Done => unreachable!(),
        };

        // verify the blob is still needed.
        assert_matches!(
            needs.next().await,
            Some(Ok(needs)) if needs == hashset! {
                pkg_contents["data/required"],
            }
        );

        // trying to start writing the blob again fails.
        assert_matches!(
            install.create_blob(pkg_contents["data/required"], BlobKind::Data).await,
            Err(BlobCreateError::ConcurrentWrite)
        );

        // no really, the blob is still needed.
        assert_matches!(
            needs.next().await,
            Some(Ok(needs)) if needs == hashset! {
                pkg_contents["data/required"],
            }
        );

        // finish writing the blob.
        assert_matches!(
            blob.write("fulfill me early please".as_bytes()).await,
            Ok(BlobWriteSuccess::Done)
        );
        closer.close().await;

        // verify there are no more needs and the package is readable.
        assert_matches!(needs.next().await, None);
        let pkg_dir = versions.open_package(pkg.meta_far_merkle_root()).await.unwrap().into_proxy();
        pkg.verify_contents(&pkg_dir).await.unwrap();

        pkgfs.stop().await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn mock_yields_expected_needs() {
        let (client, mut mock) = Client::new_mock();

        let expected = || HashRangeFull::default().take(200);

        let ((), ()) = future::join(
            async {
                mock.expect_enumerate_needs([0; 32].into())
                    .await
                    .enumerate_needs(expected().collect::<BTreeSet<_>>())
                    .await;
            },
            async {
                let needs = client.list_needs([0; 32].into());
                futures::pin_mut!(needs);

                let actual = needs.next().await.unwrap().unwrap();
                assert_eq!(actual, expected().collect::<HashSet<_>>());
            },
        )
        .await;
    }
}
