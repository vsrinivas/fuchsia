// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::repository::Repository,
    anyhow::{anyhow, Context, Result},
    errors::ffx_bail,
    fuchsia_merkle::{Hash, MerkleTreeBuilder},
    fuchsia_pkg::MetaContents,
    futures::{
        io::AsyncWriteExt,
        stream::{self, StreamExt},
        TryStreamExt,
    },
    serde_json::Value,
    std::{
        fs::File,
        path::{Path, PathBuf},
    },
    tempfile::NamedTempFile,
};

/// Download a package from a repository and write the blobs to a directory.
///
/// `repo`: Download the package from this repository.
/// `package_path`: Path to the package in the repository.
/// `output_blobs_dir`: Write the package blobs into this directory.
/// `concurrency`: Maximum number of blobs to download at the same time.
pub async fn resolve_package(
    repo: &Repository,
    package_path: &str,
    output_blobs_dir: impl AsRef<Path>,
    concurrency: usize,
) -> Result<Hash> {
    let output_blobs_dir = output_blobs_dir.as_ref();

    let desc = repo
        .get_target_description(package_path)
        .await?
        .context("missing target description here")?;

    let merkle = desc.custom().get("merkle").context("missing merkle")?;

    let meta_far_hash = if let Value::String(hash) = merkle {
        hash.parse()?
    } else {
        ffx_bail!("[Error] Merkle field is not a String. {:#?}", desc);
    };

    if !output_blobs_dir.exists() {
        async_fs::create_dir_all(output_blobs_dir).await?;
    }

    if output_blobs_dir.is_file() {
        ffx_bail!("Download path points at a file: {}", output_blobs_dir.display());
    }

    // First, download the meta.far.
    let meta_far_path =
        download_blob_to_destination(&repo, &output_blobs_dir, &meta_far_hash).await?;

    let mut archive = File::open(&meta_far_path)?;
    let mut meta_far = fuchsia_archive::Reader::new(&mut archive)?;
    let meta_contents = meta_far.read_file("meta/contents")?;
    let meta_contents = MetaContents::deserialize(meta_contents.as_slice())?.into_contents();

    // Download all the blobs.
    // FIXME(http://fxbug.dev/97192): Consider replacing this with work_queue to allow the caller to
    // globally control the concurrency.
    let mut tasks = stream::iter(
        meta_contents
            .values()
            .map(|hash| download_blob_to_destination(&repo, &output_blobs_dir, hash)),
    )
    .buffer_unordered(concurrency);

    // Wait until all the package blobs have finished downloading.
    while let Some(_) = tasks.try_next().await? {}

    Ok(meta_far_hash)
}

/// Download a blob from the repository and save it to the given
/// destination
/// `path`: Path on the server from which to download the package.
/// `repo`: A [Repository] instance.
/// `destination`: Local path to save the downloaded package.
async fn download_blob_to_destination(
    repo: &Repository,
    dir: &Path,
    blob: &Hash,
) -> Result<PathBuf> {
    let blob_str = blob.to_string();
    let path = dir.join(&blob_str);

    // If the local path already exists, check if has the correct merkle. If so, exit early.
    match async_fs::File::open(&path).await {
        Ok(mut file) => {
            let hash = fuchsia_merkle::from_async_read(&mut file).await?.root();
            if blob == &hash {
                return Ok(path);
            }
        }
        Err(err) => {
            if err.kind() != std::io::ErrorKind::NotFound {
                return Err(err.into());
            }
        }
    };

    // Otherwise download the blob into a temporary file, and validate that it has the right
    // hash.
    let mut resource =
        repo.fetch_blob(&blob_str).await.with_context(|| format!("fetching {}", blob))?;

    let (file, temp_path) = NamedTempFile::new_in(dir)?.into_parts();
    let mut file = async_fs::File::from(file);

    let mut merkle_builder = MerkleTreeBuilder::new();

    while let Some(chunk) = resource.stream.try_next().await? {
        merkle_builder.write(&chunk);
        file.write_all(&chunk).await?;
    }

    let hash = merkle_builder.finish().root();

    // Error out if the merkle doesn't match what we expected.
    if blob == &hash {
        // Flush the file to make sure all the bytes got written to disk.
        file.flush().await?;
        drop(file);

        temp_path.persist(&path)?;

        Ok(path)
    } else {
        Err(anyhow!("invalid merkle: expected {:?}, got {:?}", blob, hash))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::test_utils::{make_file_system_repository, PKG1_BIN_HASH, PKG1_HASH, PKG1_LIB_HASH},
        camino::Utf8Path,
        pretty_assertions::assert_eq,
        std::{collections::BTreeSet, fs::create_dir},
    };

    const DOWNLOAD_CONCURRENCY: usize = 5;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_resolve_package() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        // Create the repository.
        let src_repo_dir = dir.join("src");
        let src_metadata_dir = src_repo_dir.join("metadata");
        let src_blobs_dir = src_repo_dir.join("blobs");
        let repo = make_file_system_repository("tuf", &src_metadata_dir, &src_blobs_dir).await;

        // Store downloaded artifacts in this directory.
        let result_dir = dir.join("results");
        create_dir(&result_dir).unwrap();

        // Download the package.
        let meta_far_hash =
            resolve_package(&repo, "package1", &result_dir, DOWNLOAD_CONCURRENCY).await.unwrap();

        // Make sure we downloaded the right hash.
        assert_eq!(meta_far_hash.to_string(), PKG1_HASH);

        // Check that all the files got downloaded correctly.
        let result_paths = std::fs::read_dir(&result_dir)
            .unwrap()
            .map(|e| e.unwrap().path())
            .collect::<BTreeSet<_>>();

        assert_eq!(
            result_paths,
            BTreeSet::from([
                result_dir.join(PKG1_HASH).into_std_path_buf(),
                result_dir.join(PKG1_BIN_HASH).into_std_path_buf(),
                result_dir.join(PKG1_LIB_HASH).into_std_path_buf(),
            ])
        );
    }
}
