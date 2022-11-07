//! Repository implementation backed by memory

use {
    crate::{
        error::Error,
        metadata::{MetadataPath, MetadataVersion, TargetPath},
        pouf::Pouf,
        repository::{RepositoryProvider, RepositoryStorage},
        Result,
    },
    futures_io::AsyncRead,
    futures_util::{
        future::{BoxFuture, FutureExt},
        io::{AsyncReadExt, Cursor},
    },
    std::{
        collections::HashMap,
        marker::PhantomData,
        sync::{Arc, RwLock},
    },
};

/// An ephemeral repository contained solely in memory.
#[derive(Debug, Default)]
pub struct EphemeralRepository<D> {
    inner: RwLock<Inner>,
    _pouf: PhantomData<D>,
}

type MetadataMap = HashMap<(MetadataPath, MetadataVersion), Arc<[u8]>>;
type TargetsMap = HashMap<TargetPath, Arc<[u8]>>;

#[derive(Debug, Default)]
struct Inner {
    version: u64,
    metadata: MetadataMap,
    targets: TargetsMap,
}

impl<D> EphemeralRepository<D>
where
    D: Pouf,
{
    /// Create a new ephemeral repository.
    pub fn new() -> Self {
        Self {
            inner: RwLock::new(Inner {
                version: 0,
                metadata: MetadataMap::new(),
                targets: TargetsMap::new(),
            }),
            _pouf: PhantomData,
        }
    }

    /// Returns a [EphemeralBatchUpdate] for manipulating this repository. This allows callers to
    /// stage a number of mutations, and optionally atomically write them all at once.
    pub fn batch_update(&self) -> EphemeralBatchUpdate<'_, D> {
        EphemeralBatchUpdate {
            initial_parent_version: self.inner.read().unwrap().version,
            parent_repo: &self.inner,
            staging_repo: RwLock::new(Inner {
                version: 0,
                metadata: MetadataMap::new(),
                targets: TargetsMap::new(),
            }),
            _pouf: self._pouf,
        }
    }

    #[cfg(test)]
    pub(crate) fn metadata(&self) -> MetadataMap {
        self.inner.read().unwrap().metadata.clone()
    }
}

impl<D> RepositoryProvider<D> for EphemeralRepository<D>
where
    D: Pouf,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        let bytes = match self
            .inner
            .read()
            .unwrap()
            .metadata
            .get(&(meta_path.clone(), version))
        {
            Some(bytes) => Ok(Arc::clone(bytes)),
            None => Err(Error::MetadataNotFound {
                path: meta_path.clone(),
                version,
            }),
        };
        bytes_to_reader(bytes).boxed()
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &TargetPath,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        let bytes = match self.inner.read().unwrap().targets.get(target_path) {
            Some(bytes) => Ok(Arc::clone(bytes)),
            None => Err(Error::TargetNotFound(target_path.clone())),
        };
        bytes_to_reader(bytes).boxed()
    }
}

impl<D> RepositoryStorage<D> for EphemeralRepository<D>
where
    D: Pouf,
{
    fn store_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
        metadata: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, Result<()>> {
        store_metadata(&self.inner, meta_path, version, metadata)
    }

    fn store_target<'a>(
        &'a self,
        target_path: &TargetPath,
        read: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, Result<()>> {
        store_target(&self.inner, target_path, read)
    }
}

/// [EphemeralBatchUpdate] is a special repository that is designed to write the metadata and
/// targets to an [EphemeralRepository] in a single batch.
///
/// Note: `EphemeralBatchUpdate::commit()` must be called in order to write the metadata and
/// targets to the [EphemeralRepository]. Otherwise any queued changes will be lost on drop.
#[derive(Debug)]
pub struct EphemeralBatchUpdate<'a, D> {
    initial_parent_version: u64,
    parent_repo: &'a RwLock<Inner>,
    staging_repo: RwLock<Inner>,
    _pouf: PhantomData<D>,
}

/// Conflict occurred during commit.
#[derive(Debug, thiserror::Error)]
pub enum CommitError {
    // Conflicting change occurred during commit.
    #[error("conflicting change occurred during commit")]
    Conflict,
}

impl<D> EphemeralBatchUpdate<'_, D>
where
    D: Pouf,
{
    /// Write all the metadata and targets in the [EphemeralBatchUpdate] to the source
    /// [EphemeralRepository] in a single batch operation.
    pub async fn commit(self) -> std::result::Result<(), CommitError> {
        let mut parent_repo = self.parent_repo.write().unwrap();

        // Check if the parent changed while this batch was being processed.
        if self.initial_parent_version != parent_repo.version {
            return Err(CommitError::Conflict);
        }

        // Since parent hasn't changed, merged everything we wrote into its tables.
        let staging_repo = self.staging_repo.into_inner().unwrap();
        parent_repo.metadata.extend(staging_repo.metadata);
        parent_repo.targets.extend(staging_repo.targets);

        // Increment the version number because we modified the repository.
        parent_repo.version += 1;

        Ok(())
    }
}

impl<D> RepositoryProvider<D> for EphemeralBatchUpdate<'_, D>
where
    D: Pouf,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        let key = (meta_path.clone(), version);
        let bytes = if let Some(bytes) = self.staging_repo.read().unwrap().metadata.get(&key) {
            Ok(Arc::clone(bytes))
        } else {
            self.parent_repo
                .read()
                .unwrap()
                .metadata
                .get(&key)
                .map(Arc::clone)
                .ok_or_else(|| Error::MetadataNotFound {
                    path: meta_path.clone(),
                    version,
                })
        };
        bytes_to_reader(bytes).boxed()
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &TargetPath,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        let bytes = if let Some(bytes) = self.staging_repo.read().unwrap().targets.get(target_path)
        {
            Ok(Arc::clone(bytes))
        } else {
            self.parent_repo
                .read()
                .unwrap()
                .targets
                .get(target_path)
                .map(Arc::clone)
                .ok_or_else(|| Error::TargetNotFound(target_path.clone()))
        };
        bytes_to_reader(bytes).boxed()
    }
}

impl<D> RepositoryStorage<D> for EphemeralBatchUpdate<'_, D>
where
    D: Pouf,
{
    fn store_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
        metadata: &'a mut (dyn AsyncRead + Send + Unpin),
    ) -> BoxFuture<'a, Result<()>> {
        store_metadata(&self.staging_repo, meta_path, version, metadata)
    }

    fn store_target<'a>(
        &'a self,
        target_path: &TargetPath,
        read: &'a mut (dyn AsyncRead + Send + Unpin),
    ) -> BoxFuture<'a, Result<()>> {
        store_target(&self.staging_repo, target_path, read)
    }
}

fn store_metadata<'a>(
    inner: &'a RwLock<Inner>,
    meta_path: &MetadataPath,
    version: MetadataVersion,
    metadata: &'a mut (dyn AsyncRead + Send + Unpin),
) -> BoxFuture<'a, Result<()>> {
    let meta_path = meta_path.clone();
    async move {
        let mut buf = Vec::new();
        metadata.read_to_end(&mut buf).await?;
        buf.shrink_to_fit();

        let mut inner = inner.write().unwrap();

        inner.metadata.insert((meta_path, version), buf.into());

        // Increment the version since we changed.
        inner.version += 1;

        Ok(())
    }
    .boxed()
}

fn store_target<'a>(
    inner: &'a RwLock<Inner>,
    target_path: &TargetPath,
    read: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
) -> BoxFuture<'a, Result<()>> {
    let target_path = target_path.clone();
    async move {
        let mut buf = Vec::new();
        read.read_to_end(&mut buf).await?;
        buf.shrink_to_fit();

        let mut inner = inner.write().unwrap();

        inner.targets.insert(target_path, buf.into());

        // Increment the version since we changed.
        inner.version += 1;

        Ok(())
    }
    .boxed()
}

#[allow(clippy::borrowed_box)]
async fn bytes_to_reader<'a>(
    bytes: Result<Arc<[u8]>>,
) -> Result<Box<dyn AsyncRead + Send + Unpin + 'a>> {
    let bytes = bytes?;
    let reader: Box<dyn AsyncRead + Send + Unpin> = Box::new(Cursor::new(bytes));
    Ok(reader)
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::pouf::Pouf1;
    use crate::repository::{fetch_metadata_to_string, fetch_target_to_string};
    use assert_matches::assert_matches;
    use futures_executor::block_on;

    #[test]
    fn ephemeral_repo_targets() {
        block_on(async {
            let repo = EphemeralRepository::<Pouf1>::new();

            let path = TargetPath::new("batty").unwrap();
            if let Err(err) = repo.fetch_target(&path).await {
                assert_matches!(err, Error::TargetNotFound(p) if p == path);
            } else {
                panic!("expected fetch_target to fail");
            }

            let data: &[u8] = b"like tears in the rain";
            let path = TargetPath::new("batty").unwrap();
            repo.store_target(&path, &mut &*data).await.unwrap();

            let mut read = repo.fetch_target(&path).await.unwrap();
            let mut buf = Vec::new();
            read.read_to_end(&mut buf).await.unwrap();
            assert_eq!(buf.as_slice(), data);
            drop(read);

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
    fn ephemeral_repo_batch_update() {
        block_on(async {
            let repo = EphemeralRepository::<Pouf1>::new();

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
    fn ephemeral_repo_batch_commit_fails_with_metadata_conflicts() {
        block_on(async {
            let repo = EphemeralRepository::<Pouf1>::new();

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
    fn ephemeral_repo_batch_commit_fails_with_target_conflicts() {
        block_on(async {
            let repo = EphemeralRepository::<Pouf1>::new();

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
