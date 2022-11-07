use {
    crate::{
        metadata::{MetadataPath, MetadataVersion, TargetPath},
        pouf::Pouf,
        repository::{RepositoryProvider, RepositoryStorage},
        Error, Result,
    },
    futures_io::AsyncRead,
    futures_util::future::{BoxFuture, FutureExt},
    std::sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
};

pub(crate) struct ErrorRepository<R> {
    repo: R,
    fail_metadata_stores: Arc<AtomicBool>,
}

impl<R> ErrorRepository<R> {
    pub(crate) fn new(repo: R) -> Self {
        Self {
            repo,
            fail_metadata_stores: Arc::new(AtomicBool::new(false)),
        }
    }

    pub(crate) fn fail_metadata_stores(&self, fail_metadata_stores: bool) {
        self.fail_metadata_stores
            .store(fail_metadata_stores, Ordering::SeqCst);
    }
}

impl<D, R> RepositoryProvider<D> for ErrorRepository<R>
where
    R: RepositoryProvider<D> + Sync,
    D: Pouf,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        self.repo.fetch_metadata(meta_path, version)
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &TargetPath,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        self.repo.fetch_target(target_path)
    }
}

impl<D, R> RepositoryStorage<D> for ErrorRepository<R>
where
    R: RepositoryStorage<D> + Sync,
    D: Pouf,
{
    fn store_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
        metadata: &'a mut (dyn AsyncRead + Send + Unpin),
    ) -> BoxFuture<'a, Result<()>> {
        if self.fail_metadata_stores.load(Ordering::SeqCst) {
            async { Err(Error::Encoding("failed".into())) }.boxed()
        } else {
            self.repo.store_metadata(meta_path, version, metadata)
        }
    }

    fn store_target<'a>(
        &'a self,
        target_path: &TargetPath,
        target: &'a mut (dyn AsyncRead + Send + Unpin),
    ) -> BoxFuture<'a, Result<()>> {
        self.repo.store_target(target_path, target)
    }
}
