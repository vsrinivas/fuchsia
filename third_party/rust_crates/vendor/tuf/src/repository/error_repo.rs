use {
    crate::{
        crypto::{HashAlgorithm, HashValue},
        interchange::DataInterchange,
        metadata::{MetadataPath, MetadataVersion, TargetDescription, TargetPath},
        repository::{RepositoryProvider, RepositoryStorage},
        Error, Result,
    },
    futures_io::AsyncRead,
    futures_util::future::{BoxFuture, FutureExt},
    parking_lot::Mutex,
    std::sync::Arc,
};

pub(crate) struct ErrorRepository<R> {
    repo: R,
    fail_metadata_stores: Arc<Mutex<bool>>,
}

impl<R> ErrorRepository<R> {
    pub(crate) fn new(repo: R) -> Self {
        Self {
            repo,
            fail_metadata_stores: Arc::new(Mutex::new(false)),
        }
    }

    pub(crate) fn fail_metadata_stores(&self, fail_metadata_stores: bool) {
        *self.fail_metadata_stores.lock() = fail_metadata_stores;
    }
}

impl<D, R> RepositoryProvider<D> for ErrorRepository<R>
where
    R: RepositoryProvider<D> + Sync,
    D: DataInterchange + Sync,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &'a MetadataPath,
        version: &'a MetadataVersion,
        max_length: Option<usize>,
        hash_data: Option<(&'static HashAlgorithm, HashValue)>,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin>>> {
        self.repo
            .fetch_metadata(meta_path, version, max_length, hash_data)
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &'a TargetPath,
        target_description: &'a TargetDescription,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin>>> {
        self.repo.fetch_target(target_path, target_description)
    }
}

impl<D, R> RepositoryStorage<D> for ErrorRepository<R>
where
    R: RepositoryStorage<D> + Sync,
    D: DataInterchange + Sync,
{
    fn store_metadata<'a>(
        &'a self,
        meta_path: &'a MetadataPath,
        version: &'a MetadataVersion,
        metadata: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, Result<()>> {
        if *self.fail_metadata_stores.lock() {
            async { Err(Error::Encoding("failed".into())) }.boxed()
        } else {
            self.repo.store_metadata(meta_path, version, metadata)
        }
    }

    fn store_target<'a>(
        &'a self,
        target: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
        target_path: &'a TargetPath,
    ) -> BoxFuture<'a, Result<()>> {
        self.repo.store_target(target, target_path)
    }
}
