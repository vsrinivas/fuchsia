//! Repository implementation backed by a file system.

use futures_io::AsyncRead;
use futures_util::future::{BoxFuture, FutureExt};
use futures_util::io::{copy, AllowStdIo};
use log::debug;
use std::collections::HashMap;
use std::fs::{DirBuilder, File};
use std::marker::PhantomData;
use std::path::{Path, PathBuf};
use tempfile::{NamedTempFile, TempPath};

use crate::interchange::DataInterchange;
use crate::metadata::{MetadataPath, MetadataVersion, TargetPath};
use crate::repository::{RepositoryProvider, RepositoryStorage};
use crate::Result;

/// A builder to create a repository contained on the local file system.
pub struct FileSystemRepositoryBuilder<D> {
    local_path: PathBuf,
    metadata_prefix: Option<PathBuf>,
    targets_prefix: Option<PathBuf>,
    _interchange: PhantomData<D>,
}

impl<D> FileSystemRepositoryBuilder<D>
where
    D: DataInterchange,
{
    /// Create a new repository with the given `local_path` prefix.
    pub fn new<P: Into<PathBuf>>(local_path: P) -> Self {
        FileSystemRepositoryBuilder {
            local_path: local_path.into(),
            metadata_prefix: None,
            targets_prefix: None,
            _interchange: PhantomData,
        }
    }

    /// The argument `metadata_prefix` is used to provide an alternate path where metadata is
    /// stored on the repository. If `None`, this defaults to `/`. For example, if there is a TUF
    /// repository at `/usr/local/repo/`, but all metadata is stored at `/usr/local/repo/meta/`,
    /// then passing the arg `Some("meta".into())` would cause `root.json` to be fetched from
    /// `/usr/local/repo/meta/root.json`.
    pub fn metadata_prefix<P: Into<PathBuf>>(mut self, metadata_prefix: P) -> Self {
        self.metadata_prefix = Some(metadata_prefix.into());
        self
    }

    /// The argument `targets_prefix` is used to provide an alternate path where targets are
    /// stored on the repository. If `None`, this defaults to `/`. For example, if there is a TUF
    /// repository at `/usr/local/repo/`, but all targets are stored at `/usr/local/repo/targets/`,
    /// then passing the arg `Some("targets".into())` would cause `hello-world` to be fetched from
    /// `/usr/local/repo/targets/hello-world`.
    pub fn targets_prefix<P: Into<PathBuf>>(mut self, targets_prefix: P) -> Self {
        self.targets_prefix = Some(targets_prefix.into());
        self
    }

    /// Build a `FileSystemRepository`.
    pub fn build(self) -> Result<FileSystemRepository<D>> {
        let metadata_path = if let Some(metadata_prefix) = self.metadata_prefix {
            self.local_path.join(metadata_prefix)
        } else {
            self.local_path.clone()
        };
        DirBuilder::new().recursive(true).create(&metadata_path)?;

        let targets_path = if let Some(targets_prefix) = self.targets_prefix {
            self.local_path.join(targets_prefix)
        } else {
            self.local_path.clone()
        };
        DirBuilder::new().recursive(true).create(&targets_path)?;

        Ok(FileSystemRepository {
            metadata_path,
            targets_path,
            _interchange: PhantomData,
        })
    }
}

/// A repository contained on the local file system.
#[derive(Debug)]
pub struct FileSystemRepository<D>
where
    D: DataInterchange,
{
    metadata_path: PathBuf,
    targets_path: PathBuf,
    _interchange: PhantomData<D>,
}

impl<D> FileSystemRepository<D>
where
    D: DataInterchange,
{
    /// Create a [FileSystemRepositoryBuilder].
    pub fn builder<P: Into<PathBuf>>(local_path: P) -> FileSystemRepositoryBuilder<D> {
        FileSystemRepositoryBuilder::new(local_path)
    }

    /// Create a new repository on the local file system.
    pub fn new<P: Into<PathBuf>>(local_path: P) -> Result<Self> {
        FileSystemRepositoryBuilder::new(local_path)
            .metadata_prefix("metadata")
            .targets_prefix("targets")
            .build()
    }

    /// Returns a [FileSystemBatchUpdate] for manipulating this repository. This allows callers to
    /// stage a number of mutations, and optionally write them all at once.
    pub fn batch_update(&mut self) -> FileSystemBatchUpdate<D> {
        FileSystemBatchUpdate {
            repo: self,
            metadata: HashMap::new(),
            targets: HashMap::new(),
        }
    }

    fn metadata_path(&self, meta_path: &MetadataPath, version: MetadataVersion) -> PathBuf {
        let mut path = self.metadata_path.clone();
        path.extend(meta_path.components::<D>(version));
        path
    }

    fn target_path(&self, target_path: &TargetPath) -> PathBuf {
        let mut path = self.targets_path.clone();
        path.extend(target_path.components());
        path
    }

    fn fetch_path(
        &self,
        path: &Path,
    ) -> BoxFuture<'_, Result<Box<dyn AsyncRead + Send + Unpin + '_>>> {
        let reader = File::open(&path);

        async move {
            let reader = reader?;
            let reader: Box<dyn AsyncRead + Send + Unpin> = Box::new(AllowStdIo::new(reader));
            Ok(reader)
        }
        .boxed()
    }
}

impl<D> RepositoryProvider<D> for FileSystemRepository<D>
where
    D: DataInterchange + Sync,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        let path = self.metadata_path(meta_path, version);
        self.fetch_path(&path)
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &TargetPath,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        let path = self.target_path(target_path);
        self.fetch_path(&path)
    }
}

impl<D> RepositoryStorage<D> for FileSystemRepository<D>
where
    D: DataInterchange + Sync + Send,
{
    fn store_metadata<'a>(
        &'a mut self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
        metadata: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, Result<()>> {
        let path = self.metadata_path(meta_path, version);

        async move {
            if path.exists() {
                debug!("Metadata path exists. Overwriting: {:?}", path);
            }

            let mut temp_file = AllowStdIo::new(create_temp_file(&path)?);
            copy(metadata, &mut temp_file).await?;
            temp_file.into_inner().persist(&path)?;

            Ok(())
        }
        .boxed()
    }

    fn store_target<'a>(
        &'a mut self,
        target_path: &TargetPath,
        read: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, Result<()>> {
        let path = self.target_path(target_path);

        async move {
            if path.exists() {
                debug!("Target path exists. Overwriting: {:?}", path);
            }

            let mut temp_file = AllowStdIo::new(create_temp_file(&path)?);
            copy(read, &mut temp_file).await?;
            temp_file.into_inner().persist(&path)?;

            Ok(())
        }
        .boxed()
    }
}

/// [FileSystemBatchUpdate] is a special repository that is designed to write the metadata and
/// targets to an [FileSystemRepository] in a single batch.
///
/// Note: `FileSystemBatchUpdate::commit()` must be called in order to write the metadata and
/// targets to the [FileSystemRepository]. Otherwise any queued changes will be lost on drop.
#[derive(Debug)]
pub struct FileSystemBatchUpdate<'a, D: DataInterchange> {
    repo: &'a mut FileSystemRepository<D>,
    metadata: HashMap<PathBuf, TempPath>,
    targets: HashMap<PathBuf, TempPath>,
}

impl<'a, D> FileSystemBatchUpdate<'a, D>
where
    D: DataInterchange + Sync,
{
    /// Write all the metadata and targets the [FileSystemBatchUpdate] to the source
    /// [FileSystemRepository] in a single batch operation.
    ///
    /// Note: While this function will atomically write each file, it's possible that this could
    /// fail with part of the files written if we experience a system error during the process.
    pub async fn commit(self) -> Result<()> {
        for (path, tmp_path) in self.targets {
            if path.exists() {
                debug!("Target path exists. Overwriting: {:?}", path);
            }
            tmp_path.persist(path)?;
        }

        for (path, tmp_path) in self.metadata {
            if path.exists() {
                debug!("Metadata path exists. Overwriting: {:?}", path);
            }
            tmp_path.persist(path)?;
        }

        Ok(())
    }
}

impl<D> RepositoryProvider<D> for FileSystemBatchUpdate<'_, D>
where
    D: DataInterchange + Sync,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        let path = self.repo.metadata_path(meta_path, version);
        if let Some(temp_path) = self.metadata.get(&path) {
            self.repo.fetch_path(temp_path)
        } else {
            self.repo.fetch_path(&path)
        }
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &TargetPath,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        let path = self.repo.target_path(target_path);
        if let Some(temp_path) = self.targets.get(&path) {
            self.repo.fetch_path(temp_path)
        } else {
            self.repo.fetch_path(&path)
        }
    }
}

impl<D> RepositoryStorage<D> for FileSystemBatchUpdate<'_, D>
where
    D: DataInterchange + Sync,
{
    fn store_metadata<'a>(
        &'a mut self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
        read: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, Result<()>> {
        let path = self.repo.metadata_path(meta_path, version);
        let metadata = &mut self.metadata;

        async move {
            let mut temp_file = AllowStdIo::new(create_temp_file(&path)?);
            copy(read, &mut temp_file).await?;
            metadata.insert(path, temp_file.into_inner().into_temp_path());

            Ok(())
        }
        .boxed()
    }

    fn store_target<'a>(
        &'a mut self,
        target_path: &TargetPath,
        read: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, Result<()>> {
        let path = self.repo.target_path(target_path);
        let targets = &mut self.targets;

        async move {
            let mut temp_file = AllowStdIo::new(create_temp_file(&path)?);
            copy(read, &mut temp_file).await?;
            targets.insert(path, temp_file.into_inner().into_temp_path());

            Ok(())
        }
        .boxed()
    }
}

fn create_temp_file(path: &Path) -> Result<NamedTempFile> {
    // We want to atomically write the file to make sure clients can never see a partially written
    // file.  In order to do this, we'll write to a temporary file in the same directory as our
    // target, otherwise we risk writing the temporary file to one mountpoint, and then
    // non-atomically copying the file to another mountpoint.

    if let Some(parent) = path.parent() {
        DirBuilder::new().recursive(true).create(parent)?;
        Ok(NamedTempFile::new_in(parent)?)
    } else {
        Ok(NamedTempFile::new_in(".")?)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::error::Error;
    use crate::interchange::Json;
    use crate::metadata::RootMetadata;
    use crate::repository::{fetch_metadata_to_string, fetch_target_to_string, Repository};
    use assert_matches::assert_matches;
    use futures_executor::block_on;
    use futures_util::io::AsyncReadExt;
    use tempfile;

    #[test]
    fn file_system_repo_metadata_not_found_error() {
        block_on(async {
            let temp_dir = tempfile::Builder::new()
                .prefix("rust-tuf")
                .tempdir()
                .unwrap();
            let repo = FileSystemRepositoryBuilder::new(temp_dir.path())
                .build()
                .unwrap();

            assert_matches!(
                Repository::<_, Json>::new(repo)
                    .fetch_metadata::<RootMetadata>(
                        &MetadataPath::root(),
                        MetadataVersion::None,
                        None,
                        vec![],
                    )
                    .await,
                Err(Error::NotFound)
            );
        })
    }

    #[test]
    fn file_system_repo_targets() {
        block_on(async {
            let temp_dir = tempfile::Builder::new()
                .prefix("rust-tuf")
                .tempdir()
                .unwrap();
            let mut repo = FileSystemRepositoryBuilder::<Json>::new(temp_dir.path().to_path_buf())
                .metadata_prefix("meta")
                .targets_prefix("targs")
                .build()
                .unwrap();

            // test that init worked
            assert!(temp_dir.path().join("meta").exists());
            assert!(temp_dir.path().join("targs").exists());

            let data: &[u8] = b"like tears in the rain";
            let path = TargetPath::new("foo/bar/baz").unwrap();
            repo.store_target(&path, &mut &*data).await.unwrap();
            assert!(temp_dir
                .path()
                .join("targs")
                .join("foo")
                .join("bar")
                .join("baz")
                .exists());

            let mut buf = Vec::new();

            // Enclose `fetch_target` in a scope to make sure the file is closed.
            // This is needed for `tempfile` on Windows, which doesn't open the
            // files in a mode that allows the file to be opened multiple times.
            {
                let mut read = repo.fetch_target(&path).await.unwrap();
                read.read_to_end(&mut buf).await.unwrap();
                assert_eq!(buf.as_slice(), data);
            }

            // RepositoryProvider implementations do not guarantee data is not corrupt.
            let bad_data: &[u8] = b"you're in a desert";
            repo.store_target(&path, &mut &*bad_data).await.unwrap();
            let mut read = repo.fetch_target(&path).await.unwrap();
            buf.clear();
            read.read_to_end(&mut buf).await.unwrap();
            assert_eq!(buf.as_slice(), bad_data);
        })
    }

    #[test]
    fn file_system_repo_batch_update() {
        block_on(async {
            let temp_dir = tempfile::Builder::new()
                .prefix("rust-tuf")
                .tempdir()
                .unwrap();
            let mut repo = FileSystemRepositoryBuilder::<Json>::new(temp_dir.path().to_path_buf())
                .metadata_prefix("meta")
                .targets_prefix("targs")
                .build()
                .unwrap();

            let meta_path = MetadataPath::new("meta").unwrap();
            let meta_version = MetadataVersion::None;
            let target_path = TargetPath::new("target").unwrap();

            // First, write some stuff to the repository.
            let committed_meta = "committed meta";
            let committed_target = "committed target";

            repo.store_metadata(&meta_path, meta_version, &mut committed_meta.as_bytes())
                .await
                .unwrap();

            repo.store_target(&target_path, &mut committed_target.as_bytes())
                .await
                .unwrap();

            let mut batch = repo.batch_update();

            // Make sure we can read back the committed stuff.
            assert_eq!(
                fetch_metadata_to_string(&batch, &meta_path, meta_version)
                    .await
                    .unwrap(),
                committed_meta,
            );
            assert_eq!(
                fetch_target_to_string(&batch, &target_path).await.unwrap(),
                committed_target,
            );

            // Next, stage some stuff in the batch_update.
            let staged_meta = "staged meta";
            let staged_target = "staged target";
            batch
                .store_metadata(&meta_path, meta_version, &mut staged_meta.as_bytes())
                .await
                .unwrap();
            batch
                .store_target(&target_path, &mut staged_target.as_bytes())
                .await
                .unwrap();

            // Make sure it got staged.
            assert_eq!(
                fetch_metadata_to_string(&batch, &meta_path, meta_version)
                    .await
                    .unwrap(),
                staged_meta,
            );
            assert_eq!(
                fetch_target_to_string(&batch, &target_path).await.unwrap(),
                staged_target,
            );

            // Next, drop the batch_update. We shouldn't have written the data back to the
            // repository.
            drop(batch);

            assert_eq!(
                fetch_metadata_to_string(&repo, &meta_path, meta_version)
                    .await
                    .unwrap(),
                committed_meta,
            );
            assert_eq!(
                fetch_target_to_string(&repo, &target_path).await.unwrap(),
                committed_target,
            );

            // Do the batch_update again, but this time write the data.
            let mut batch = repo.batch_update();
            batch
                .store_metadata(&meta_path, meta_version, &mut staged_meta.as_bytes())
                .await
                .unwrap();
            batch
                .store_target(&target_path, &mut staged_target.as_bytes())
                .await
                .unwrap();
            batch.commit().await.unwrap();

            // Make sure the new data got to the repository.
            assert_eq!(
                fetch_metadata_to_string(&repo, &meta_path, meta_version)
                    .await
                    .unwrap(),
                staged_meta,
            );
            assert_eq!(
                fetch_target_to_string(&repo, &target_path).await.unwrap(),
                staged_target,
            );
        })
    }
}
