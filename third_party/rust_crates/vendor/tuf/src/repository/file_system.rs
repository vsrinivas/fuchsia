//! Repository implementation backed by a file system.

use {
    crate::{
        error::{Error, Result},
        metadata::{MetadataPath, MetadataVersion, TargetPath},
        pouf::Pouf,
        repository::{RepositoryProvider, RepositoryStorage},
    },
    futures_io::AsyncRead,
    futures_util::future::{BoxFuture, FutureExt},
    futures_util::io::{copy, AllowStdIo},
    log::debug,
    std::{
        collections::HashMap,
        fs::{DirBuilder, File},
        io,
        marker::PhantomData,
        path::{Path, PathBuf},
        sync::RwLock,
    },
    tempfile::{NamedTempFile, TempPath},
};

/// A builder to create a repository contained on the local file system.
pub struct FileSystemRepositoryBuilder<D> {
    local_path: PathBuf,
    metadata_prefix: Option<PathBuf>,
    targets_prefix: Option<PathBuf>,
    _pouf: PhantomData<D>,
}

impl<D> FileSystemRepositoryBuilder<D>
where
    D: Pouf,
{
    /// Create a new repository with the given `local_path` prefix.
    pub fn new<P: Into<PathBuf>>(local_path: P) -> Self {
        FileSystemRepositoryBuilder {
            local_path: local_path.into(),
            metadata_prefix: None,
            targets_prefix: None,
            _pouf: PhantomData,
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
    pub fn build(self) -> FileSystemRepository<D> {
        let metadata_path = if let Some(metadata_prefix) = self.metadata_prefix {
            self.local_path.join(metadata_prefix)
        } else {
            self.local_path.clone()
        };

        let targets_path = if let Some(targets_prefix) = self.targets_prefix {
            self.local_path.join(targets_prefix)
        } else {
            self.local_path.clone()
        };

        FileSystemRepository {
            version: RwLock::new(0),
            metadata_path,
            targets_path,
            _pouf: PhantomData,
        }
    }
}

/// A repository contained on the local file system.
#[derive(Debug)]
pub struct FileSystemRepository<D>
where
    D: Pouf,
{
    version: RwLock<u64>,
    metadata_path: PathBuf,
    targets_path: PathBuf,
    _pouf: PhantomData<D>,
}

impl<D> FileSystemRepository<D>
where
    D: Pouf,
{
    /// Create a [FileSystemRepositoryBuilder].
    pub fn builder<P: Into<PathBuf>>(local_path: P) -> FileSystemRepositoryBuilder<D> {
        FileSystemRepositoryBuilder::new(local_path)
    }

    /// Create a new repository on the local file system.
    pub fn new<P: Into<PathBuf>>(local_path: P) -> Self {
        FileSystemRepositoryBuilder::new(local_path)
            .metadata_prefix("metadata")
            .targets_prefix("targets")
            .build()
    }

    /// Returns a [FileSystemBatchUpdate] for manipulating this repository. This allows callers to
    /// stage a number of mutations, and optionally write them all at once.
    ///
    /// [FileSystemBatchUpdate] will try to update any changed metadata or targets in a
    /// single transaction, and will fail if there are any conflict writes, either by directly
    /// calling [FileSystemRepository::store_metadata], [FileSystemRepository::store_target], or
    /// another [FileSystemRepository::batch_update].
    ///
    /// Warning: The current implementation makes no effort to prevent manipulations of the
    /// underlying filesystem, either in-process, or by an external process.
    pub fn batch_update(&self) -> FileSystemBatchUpdate<D> {
        FileSystemBatchUpdate {
            initial_parent_version: *self.version.read().unwrap(),
            parent_repo: self,
            metadata: RwLock::new(HashMap::new()),
            targets: RwLock::new(HashMap::new()),
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

    fn fetch_metadata_from_path(
        &self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
        path: &Path,
    ) -> BoxFuture<'_, Result<Box<dyn AsyncRead + Send + Unpin + '_>>> {
        let reader = File::open(&path).map_err(|err| {
            if err.kind() == io::ErrorKind::NotFound {
                Error::MetadataNotFound {
                    path: meta_path.clone(),
                    version,
                }
            } else {
                Error::IoPath {
                    path: path.to_path_buf(),
                    err,
                }
            }
        });

        async move {
            let reader = reader?;
            let reader: Box<dyn AsyncRead + Send + Unpin> = Box::new(AllowStdIo::new(reader));
            Ok(reader)
        }
        .boxed()
    }

    fn fetch_target_from_path(
        &self,
        target_path: &TargetPath,
        path: &Path,
    ) -> BoxFuture<'_, Result<Box<dyn AsyncRead + Send + Unpin + '_>>> {
        let reader = File::open(&path).map_err(|err| {
            if err.kind() == io::ErrorKind::NotFound {
                Error::TargetNotFound(target_path.clone())
            } else {
                Error::IoPath {
                    path: path.to_path_buf(),
                    err,
                }
            }
        });

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
    D: Pouf,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        let path = self.metadata_path(meta_path, version);
        self.fetch_metadata_from_path(meta_path, version, &path)
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &TargetPath,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        let path = self.target_path(target_path);
        self.fetch_target_from_path(target_path, &path)
    }
}

impl<D> RepositoryStorage<D> for FileSystemRepository<D>
where
    D: Pouf,
{
    fn store_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
        metadata: &'a mut (dyn AsyncRead + Send + Unpin),
    ) -> BoxFuture<'a, Result<()>> {
        let path = self.metadata_path(meta_path, version);

        async move {
            if path.exists() {
                debug!("Metadata path exists. Overwriting: {:?}", path);
            }

            let mut temp_file = AllowStdIo::new(create_temp_file(&path)?);
            if let Err(err) = copy(metadata, &mut temp_file).await {
                return Err(Error::IoPath { path, err });
            }

            // Lock the version counter to prevent other writers from manipulating the repository to
            // avoid race conditions.
            let mut version = self.version.write().unwrap();

            temp_file
                .into_inner()
                .persist(&path)
                .map_err(|err| Error::IoPath {
                    path,
                    err: err.error,
                })?;

            // Increment our version since the repository changed.
            *version += 1;

            Ok(())
        }
        .boxed()
    }

    fn store_target<'a>(
        &'a self,
        target_path: &TargetPath,
        read: &'a mut (dyn AsyncRead + Send + Unpin),
    ) -> BoxFuture<'a, Result<()>> {
        let path = self.target_path(target_path);

        async move {
            if path.exists() {
                debug!("Target path exists. Overwriting: {:?}", path);
            }

            let mut temp_file = AllowStdIo::new(create_temp_file(&path)?);
            if let Err(err) = copy(read, &mut temp_file).await {
                return Err(Error::IoPath { path, err });
            }

            let mut version = self.version.write().unwrap();

            temp_file
                .into_inner()
                .persist(&path)
                .map_err(|err| Error::IoPath {
                    path,
                    err: err.error,
                })?;

            // Increment our version since the repository changed.
            *version += 1;

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
pub struct FileSystemBatchUpdate<'a, D: Pouf> {
    initial_parent_version: u64,
    parent_repo: &'a FileSystemRepository<D>,
    metadata: RwLock<HashMap<PathBuf, TempPath>>,
    targets: RwLock<HashMap<PathBuf, TempPath>>,
}

#[derive(Debug, thiserror::Error)]
pub enum CommitError {
    /// Conflict occurred during commit.
    #[error("conflicting change occurred during commit")]
    Conflict,

    #[error(transparent)]
    Io(#[from] std::io::Error),

    /// An IO error occurred for a path.
    #[error("IO error on path {path}")]
    IoPath {
        /// Path where the error occurred.
        path: std::path::PathBuf,

        /// The IO error.
        #[source]
        err: io::Error,
    },
}

impl<'a, D> FileSystemBatchUpdate<'a, D>
where
    D: Pouf,
{
    /// Write all the metadata and targets the [FileSystemBatchUpdate] to the source
    /// [FileSystemRepository] in a single batch operation.
    ///
    /// Note: While this function will atomically write each file, it's possible that this could
    /// fail with part of the files written if we experience a system error during the process.
    pub async fn commit(self) -> std::result::Result<(), CommitError> {
        let mut parent_version = self.parent_repo.version.write().unwrap();

        if self.initial_parent_version != *parent_version {
            return Err(CommitError::Conflict);
        }

        for (path, tmp_path) in self.targets.into_inner().unwrap() {
            if path.exists() {
                debug!("Target path exists. Overwriting: {:?}", path);
            }
            tmp_path.persist(&path).map_err(|err| CommitError::IoPath {
                path,
                err: err.error,
            })?;
        }

        for (path, tmp_path) in self.metadata.into_inner().unwrap() {
            if path.exists() {
                debug!("Metadata path exists. Overwriting: {:?}", path);
            }
            tmp_path.persist(&path).map_err(|err| CommitError::IoPath {
                path,
                err: err.error,
            })?;
        }

        // Increment the version because we wrote to it.
        *parent_version += 1;

        Ok(())
    }
}

impl<D> RepositoryProvider<D> for FileSystemBatchUpdate<'_, D>
where
    D: Pouf,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        let path = self.parent_repo.metadata_path(meta_path, version);
        if let Some(temp_path) = self.metadata.read().unwrap().get(&path) {
            self.parent_repo
                .fetch_metadata_from_path(meta_path, version, temp_path)
        } else {
            self.parent_repo
                .fetch_metadata_from_path(meta_path, version, &path)
        }
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &TargetPath,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        let path = self.parent_repo.target_path(target_path);
        if let Some(temp_path) = self.targets.read().unwrap().get(&path) {
            self.parent_repo
                .fetch_target_from_path(target_path, temp_path)
        } else {
            self.parent_repo.fetch_target_from_path(target_path, &path)
        }
    }
}

impl<D> RepositoryStorage<D> for FileSystemBatchUpdate<'_, D>
where
    D: Pouf,
{
    fn store_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
        read: &'a mut (dyn AsyncRead + Send + Unpin),
    ) -> BoxFuture<'a, Result<()>> {
        let path = self.parent_repo.metadata_path(meta_path, version);

        async move {
            let mut temp_file = AllowStdIo::new(create_temp_file(&path)?);
            if let Err(err) = copy(read, &mut temp_file).await {
                return Err(Error::IoPath { path, err });
            }
            self.metadata
                .write()
                .unwrap()
                .insert(path, temp_file.into_inner().into_temp_path());

            Ok(())
        }
        .boxed()
    }

    fn store_target<'a>(
        &'a self,
        target_path: &TargetPath,
        read: &'a mut (dyn AsyncRead + Send + Unpin),
    ) -> BoxFuture<'a, Result<()>> {
        let path = self.parent_repo.target_path(target_path);

        async move {
            let mut temp_file = AllowStdIo::new(create_temp_file(&path)?);
            if let Err(err) = copy(read, &mut temp_file).await {
                return Err(Error::IoPath { path, err });
            }
            self.targets
                .write()
                .unwrap()
                .insert(path, temp_file.into_inner().into_temp_path());

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
        DirBuilder::new()
            .recursive(true)
            .create(parent)
            .map_err(|err| Error::IoPath {
                path: parent.to_path_buf(),
                err,
            })?;
        Ok(NamedTempFile::new_in(parent).map_err(|err| Error::IoPath {
            path: parent.to_path_buf(),
            err,
        })?)
    } else {
        Ok(NamedTempFile::new_in(".").map_err(|err| Error::IoPath {
            path: path.to_path_buf(),
            err,
        })?)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::error::Error;
    use crate::metadata::RootMetadata;
    use crate::pouf::Pouf1;
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
            let repo = FileSystemRepositoryBuilder::new(temp_dir.path()).build();

            assert_matches!(
                Repository::<_, Pouf1>::new(repo)
                    .fetch_metadata::<RootMetadata>(
                        &MetadataPath::root(),
                        MetadataVersion::None,
                        None,
                        vec![],
                    )
                    .await,
                Err(Error::MetadataNotFound {
                    path,
                    version,
                })
                if path == MetadataPath::root() && version == MetadataVersion::None
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
            let repo = FileSystemRepositoryBuilder::<Pouf1>::new(temp_dir.path().to_path_buf())
                .metadata_prefix("meta")
                .targets_prefix("targs")
                .build();

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

            let repo = FileSystemRepositoryBuilder::<Pouf1>::new(temp_dir.path().to_path_buf())
                .metadata_prefix("meta")
                .targets_prefix("targs")
                .build();

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

            let batch = repo.batch_update();

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
            let batch = repo.batch_update();
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

    #[test]
    fn file_system_repo_batch_commit_fails_with_metadata_conflicts() {
        block_on(async {
            let temp_dir = tempfile::Builder::new()
                .prefix("rust-tuf")
                .tempdir()
                .unwrap();

            let repo = FileSystemRepository::<Pouf1>::new(temp_dir.path().to_path_buf());

            // commit() fails if we did nothing to the batch, but the repo changed.
            let batch = repo.batch_update();

            repo.store_metadata(
                &MetadataPath::new("meta1").unwrap(),
                MetadataVersion::None,
                &mut "meta1".as_bytes(),
            )
            .await
            .unwrap();

            assert_matches!(batch.commit().await, Err(CommitError::Conflict));

            // writing to both the repo and the batch should conflict.
            let batch = repo.batch_update();

            repo.store_metadata(
                &MetadataPath::new("meta2").unwrap(),
                MetadataVersion::None,
                &mut "meta2".as_bytes(),
            )
            .await
            .unwrap();

            batch
                .store_metadata(
                    &MetadataPath::new("meta3").unwrap(),
                    MetadataVersion::None,
                    &mut "meta3".as_bytes(),
                )
                .await
                .unwrap();

            assert_matches!(batch.commit().await, Err(CommitError::Conflict));
        })
    }

    #[test]
    fn file_system_repo_batch_commit_fails_with_target_conflicts() {
        block_on(async {
            let temp_dir = tempfile::Builder::new()
                .prefix("rust-tuf")
                .tempdir()
                .unwrap();

            let repo = FileSystemRepository::<Pouf1>::new(temp_dir.path().to_path_buf());

            // commit() fails if we did nothing to the batch, but the repo changed.
            let batch = repo.batch_update();

            repo.store_target(
                &TargetPath::new("target1").unwrap(),
                &mut "target1".as_bytes(),
            )
            .await
            .unwrap();

            assert_matches!(batch.commit().await, Err(CommitError::Conflict));

            // writing to both the repo and the batch should conflict.
            let batch = repo.batch_update();

            repo.store_target(
                &TargetPath::new("target2").unwrap(),
                &mut "target2".as_bytes(),
            )
            .await
            .unwrap();

            batch
                .store_target(
                    &TargetPath::new("target3").unwrap(),
                    &mut "target3".as_bytes(),
                )
                .await
                .unwrap();

            assert_matches!(batch.commit().await, Err(CommitError::Conflict));

            // multiple batches should conflict.
            let batch1 = repo.batch_update();
            let batch2 = repo.batch_update();

            batch1
                .store_target(
                    &TargetPath::new("target4").unwrap(),
                    &mut "target4".as_bytes(),
                )
                .await
                .unwrap();

            batch2
                .store_target(
                    &TargetPath::new("target5").unwrap(),
                    &mut "target5".as_bytes(),
                )
                .await
                .unwrap();

            assert_matches!(batch1.commit().await, Ok(()));
            assert_matches!(batch2.commit().await, Err(CommitError::Conflict));
        })
    }
}
