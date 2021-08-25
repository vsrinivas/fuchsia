// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Types to track and manage indices of packages.

use {
    fuchsia_merkle::Hash,
    fuchsia_pkg::{MetaContents, MetaPackage, PackagePath},
    std::collections::HashSet,
};

mod dynamic;
mod package;
mod retained;

pub use package::{
    fulfill_meta_far_blob, load_cache_packages, set_retained_index, CompleteInstallError,
    FulfillMetaFarError, PackageIndex,
};

#[derive(thiserror::Error, Debug)]
pub enum QueryPackageMetadataError {
    #[error("failed to open blob")]
    OpenBlob(#[source] io_util::node::OpenError),

    #[error("failed to parse meta far")]
    ParseMetaFar(#[from] fuchsia_archive::Error),

    #[error("failed to parse meta package")]
    ParseMetaPackage(#[from] fuchsia_pkg::MetaPackageError),

    #[error("failed to parse meta contents")]
    ParseMetaContents(#[from] fuchsia_pkg::MetaContentsError),
}

/// Parses the meta far blob, if it exists in blobfs, returning the package path in meta/package and
/// the set of all content blobs specified in meta/contents.
async fn enumerate_package_blobs(
    blobfs: &blobfs::Client,
    meta_hash: &Hash,
) -> Result<Option<(PackagePath, HashSet<Hash>)>, QueryPackageMetadataError> {
    let file = match blobfs.open_blob_for_read(&meta_hash).await {
        Ok(file) => file,
        Err(io_util::node::OpenError::OpenError(fuchsia_zircon::Status::NOT_FOUND)) => {
            return Ok(None)
        }
        Err(e) => return Err(QueryPackageMetadataError::OpenBlob(e)),
    };

    let mut meta_far =
        fuchsia_archive::AsyncReader::new(io_util::file::AsyncFile::from_proxy(file)).await?;
    let meta_package = MetaPackage::deserialize(&meta_far.read_file("meta/package").await?[..])?;
    let meta_contents = MetaContents::deserialize(&meta_far.read_file("meta/contents").await?[..])?;

    Ok(Some((
        meta_package.into_path(),
        meta_contents.into_hashes_undeduplicated().collect::<HashSet<_>>(),
    )))
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::test_utils::add_meta_far_to_blobfs, fuchsia_async as fasync,
        maplit::hashset,
    };

    #[fasync::run_singlethreaded(test)]
    async fn enumerate_package_blobs_and_meta_far_exists() {
        let meta_far_hash = Hash::from([2; 32]);
        let path = PackagePath::from_name_and_variant("fake-package", "0").unwrap();

        let (blobfs_fake, blobfs) = fuchsia_pkg_testing::blobfs::Fake::new();

        let blob_hash0 = Hash::from([3; 32]);
        let blob_hash1 = Hash::from([4; 32]);
        add_meta_far_to_blobfs(
            &blobfs_fake,
            meta_far_hash,
            "fake-package",
            vec![blob_hash0, blob_hash1],
        );

        let res = enumerate_package_blobs(&blobfs, &meta_far_hash).await.unwrap();

        assert_eq!(res, Some((path, hashset! {blob_hash0, blob_hash1})));
    }

    #[fasync::run_singlethreaded(test)]
    async fn enumerate_package_blobs_and_missing_meta_far() {
        let meta_far_hash = Hash::from([2; 32]);
        let (_blobfs_fake, blobfs) = fuchsia_pkg_testing::blobfs::Fake::new();

        let res = enumerate_package_blobs(&blobfs, &meta_far_hash).await.unwrap();

        assert_eq!(res, None);
    }
}
