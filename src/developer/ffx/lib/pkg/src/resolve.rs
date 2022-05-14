// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::repository::{get_tuf_client, Repository},
    anyhow::{anyhow, Context, Result},
    async_lock::Mutex,
    errors::ffx_bail,
    fuchsia_merkle::{Hash, MerkleTreeBuilder},
    fuchsia_pkg::MetaContents,
    futures::{
        future::BoxFuture,
        io::Cursor,
        stream::{self, StreamExt},
        AsyncRead, AsyncReadExt as _, AsyncWriteExt as _, FutureExt as _, TryStreamExt as _,
    },
    serde_json::Value,
    std::{
        fs::File,
        path::{Path, PathBuf},
        sync::Arc,
    },
    tempfile::NamedTempFile,
    tuf::{
        interchange::DataInterchange,
        metadata::{MetadataPath, MetadataVersion, TargetDescription, TargetPath},
        repository::{FileSystemRepository, RepositoryProvider, RepositoryStorage},
        Error,
    },
};

/// Download the metadata and all package blobs from a repository.
///
/// `repo`: Download the package from this repository.
/// `metadata_dir`: Write repository metadata to this directory.
/// `output_blobs_dir`: Write the package blobs into this directory.
/// `concurrency`: Maximum number of blobs to download at the same time.
pub async fn resolve_repository(
    repo: &Repository,
    metadata_dir: impl AsRef<Path>,
    blobs_dir: impl AsRef<Path>,
    concurrency: usize,
) -> Result<()> {
    let metadata_dir = metadata_dir.as_ref();
    let blobs_dir = blobs_dir.as_ref();

    let upstream_repo = repo.get_tuf_repo()?;

    // Cache the TUF metadata from the upstream repository into a temporary directory.
    let mut local_repo =
        FileSystemRepository::builder(&metadata_dir).targets_prefix("targets").build()?;

    let mut batch_repo = local_repo.batch_update();

    let trusted_targets = {
        let cache_repo = CacheRepository::new(upstream_repo, &mut batch_repo);

        // Fetch the metadata, which will verify that it is correct.
        let mut tuf_client = get_tuf_client(cache_repo).await?;
        tuf_client.update().await?;

        tuf_client.database().trusted_targets().cloned()
    };

    // Commit the metadata after we've verified the TUF metadata.
    batch_repo.commit().await?;

    // Exit early if there are no targets.
    if let Some(trusted_targets) = trusted_targets {
        // Download all the packages.
        for desc in trusted_targets.targets().values() {
            let merkle = merkle_from_description(&desc)?;
            fetch_package(repo, merkle, &blobs_dir, concurrency).await?;
        }
    };

    Ok(())
}

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

    let meta_far_hash = merkle_from_description(&desc)?;

    fetch_package(repo, meta_far_hash, &output_blobs_dir, concurrency).await
}

pub async fn fetch_package(
    repo: &Repository,
    meta_far_hash: Hash,
    output_blobs_dir: impl AsRef<Path>,
    concurrency: usize,
) -> Result<Hash> {
    let output_blobs_dir = output_blobs_dir.as_ref();

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

struct CacheRepository<U, C> {
    upstream: U,
    cache: Arc<Mutex<C>>,
}

impl<U, C> CacheRepository<U, C> {
    /// Construct a [CacheRepository].
    pub(crate) fn new(upstream: U, cache: C) -> Self {
        Self { upstream, cache: Arc::new(Mutex::new(cache)) }
    }
}

impl<D, U, C> RepositoryProvider<D> for CacheRepository<U, C>
where
    D: DataInterchange + Sync,
    U: RepositoryProvider<D> + Send + Sync,
    C: RepositoryStorage<D> + Send + Sync,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>, Error>> {
        let meta_path = meta_path.clone();
        let metadata_fut = self.upstream.fetch_metadata(&meta_path, version.clone());

        let cache = Arc::clone(&self.cache);

        async move {
            let mut metadata = metadata_fut.await?;

            let mut bytes = vec![];
            metadata.read_to_end(&mut bytes).await?;

            let mut cache = cache.lock().await;
            cache.store_metadata(&meta_path, version, &mut Cursor::new(&bytes)).await?;

            let reader: Box<dyn AsyncRead + Send + Unpin> = Box::new(Cursor::new(bytes));
            Ok(reader)
        }
        .boxed()
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &TargetPath,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>, Error>> {
        let target_path = target_path.clone();
        let target_fut = self.upstream.fetch_target(&target_path);

        let cache = Arc::clone(&self.cache);

        async move {
            let mut target = target_fut.await?;

            let mut bytes = vec![];
            target.read_to_end(&mut bytes).await?;

            let mut cache = cache.lock().await;
            cache.store_target(&target_path, &mut Cursor::new(&bytes)).await?;

            let reader: Box<dyn AsyncRead + Send + Unpin> = Box::new(Cursor::new(bytes));
            Ok(reader)
        }
        .boxed()
    }
}

fn merkle_from_description(desc: &TargetDescription) -> Result<Hash> {
    let merkle = desc.custom().get("merkle").context("missing merkle")?;

    if let Value::String(hash) = merkle {
        Ok(hash.parse()?)
    } else {
        ffx_bail!("Merkle field is not a String. {:#?}", desc)
    }
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_resolve_repository() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        // First construct a repository that we'll download.
        let src_repo_dir = dir.join("src");
        let src_metadata_dir = src_repo_dir.join("metadata");
        let src_blobs_dir = src_repo_dir.join("blobs");
        let repo = make_file_system_repository("tuf", &src_metadata_dir, &src_blobs_dir).await;

        // Download the repository.
        let dst_repo_dir = dir.join("dst");
        let dst_metadata_dir = dst_repo_dir.join("metadata");
        let dst_blobs_dir = dst_repo_dir.join("blobs");
        resolve_repository(&repo, &dst_metadata_dir, &dst_blobs_dir, DOWNLOAD_CONCURRENCY)
            .await
            .unwrap();

        // Make sure that we downloaded all the metadata. We don't compare the directories because
        // make_repository also produces target files, or unversioned metadata.
        for name in ["1.root.json", "1.targets.json", "1.snapshot.json", "timestamp.json"] {
            let src_path = src_metadata_dir.join(name);
            let dst_path = dst_metadata_dir.join(name);

            let src = std::fs::read_to_string(&src_path).unwrap();
            let dst = std::fs::read_to_string(&dst_path).unwrap();

            assert_eq!(src, dst);
        }

        // Make sure all the blobs were fetched.
        for entry in std::fs::read_dir(&src_blobs_dir).unwrap() {
            let entry = entry.unwrap();
            let src_path = entry.path();

            let blob = src_path.strip_prefix(&src_blobs_dir).unwrap();
            let dst_path = dst_blobs_dir.as_std_path().join(blob);

            let src = std::fs::read(&src_path).unwrap();
            let dst = std::fs::read(&dst_path).unwrap();

            assert_eq!(src, dst);
        }
    }
}
