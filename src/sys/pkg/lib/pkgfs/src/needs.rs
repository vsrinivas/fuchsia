// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around the /pkgfs/needs filesystem.

use {
    crate::iou,
    failure::Fail,
    fidl_fuchsia_io::DirectoryProxy,
    fuchsia_merkle::{Hash, ParseHashError},
    fuchsia_zircon::Status,
    futures::prelude::*,
    std::collections::HashSet,
};

#[derive(Debug, Fail)]
#[allow(missing_docs)]
pub enum ListNeedsError {
    #[fail(display = "while opening needs dir: {}", _0)]
    OpenDir(#[cause] iou::OpenError),

    #[fail(display = "while listing needs dir: {}", _0)]
    ReadDir(#[cause] files_async::Error),

    #[fail(display = "unable to parse a need blob id: {}", _0)]
    ParseError(#[cause] ParseHashError),
}

/// An open handle to /pkgfs/needs
#[derive(Debug, Clone)]
pub struct Client {
    proxy: DirectoryProxy,
}

impl Client {
    /// Returns an client connected to pkgfs from the current component's namespace
    pub fn open_from_namespace() -> Result<Self, failure::Error> {
        let proxy = iou::open_directory_from_namespace("/pkgfs/needs")?;
        Ok(Client { proxy })
    }

    /// Returns an client connected to pkgfs from the given pkgfs root dir.
    pub fn open_from_pkgfs_root(pkgfs: &DirectoryProxy) -> Result<Self, failure::Error> {
        Ok(Client {
            proxy: iou::open_directory_no_describe(
                pkgfs,
                "needs",
                fidl_fuchsia_io::OPEN_RIGHT_READABLE,
            )?,
        })
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

    let needs_dir = match iou::open_directory(pkgfs_needs, &path, flags).await {
        Ok(dir) => dir,
        Err(iou::OpenError::OpenError(Status::NOT_FOUND)) => return Ok(HashSet::new()),
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

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::install::{BlobKind, BlobWriteSuccess},
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
            versions.open_package(pkg1.meta_far_merkle_root().to_owned(), None).await.unwrap();
        pkg1.verify_contents(&pkg1_dir).await.unwrap();

        let pkg2_dir =
            versions.open_package(pkg2.meta_far_merkle_root().to_owned(), None).await.unwrap();
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
}
