use {
    crate::{
        metadata::{Metadata, MetadataPath, MetadataVersion, RawSignedMetadata, TargetPath},
        pouf::Pouf,
        repository::{RepositoryProvider, RepositoryStorage},
        Result,
    },
    futures_io::AsyncRead,
    futures_util::{
        future::{BoxFuture, FutureExt},
        io::{AsyncReadExt, Cursor},
    },
    std::sync::Mutex,
};

#[derive(Debug, PartialEq)]
pub(crate) enum Track {
    Store {
        path: MetadataPath,
        version: MetadataVersion,
        metadata: String,
    },
    FetchFound {
        path: MetadataPath,
        version: MetadataVersion,
        metadata: String,
    },
    FetchErr(MetadataPath, MetadataVersion),
}

impl Track {
    pub(crate) fn store<T>(meta_path: &MetadataPath, version: MetadataVersion, metadata: T) -> Self
    where
        T: Into<Vec<u8>>,
    {
        Track::Store {
            path: meta_path.clone(),
            version,
            metadata: String::from_utf8(metadata.into()).unwrap(),
        }
    }

    pub(crate) fn store_meta<M, D>(
        version: MetadataVersion,
        metadata: &RawSignedMetadata<D, M>,
    ) -> Self
    where
        M: Metadata,
        D: Pouf,
    {
        Self::store(&M::ROLE.into(), version, metadata.as_bytes())
    }

    pub(crate) fn fetch_found<T>(
        meta_path: &MetadataPath,
        version: MetadataVersion,
        metadata: T,
    ) -> Self
    where
        T: Into<Vec<u8>>,
    {
        Track::FetchFound {
            path: meta_path.clone(),
            version,
            metadata: String::from_utf8(metadata.into()).unwrap(),
        }
    }

    pub(crate) fn fetch_meta_found<M, D>(
        version: MetadataVersion,
        metadata: &RawSignedMetadata<D, M>,
    ) -> Self
    where
        M: Metadata,
        D: Pouf,
    {
        Track::fetch_found(&M::ROLE.into(), version, metadata.as_bytes())
    }
}

/// Helper Repository wrapper that tracks all the metadata fetches and stores for testing purposes.
pub(crate) struct TrackRepository<R> {
    repo: R,
    tracks: Mutex<Vec<Track>>,
}

impl<R> TrackRepository<R> {
    pub(crate) fn new(repo: R) -> Self {
        Self {
            repo,
            tracks: Mutex::new(vec![]),
        }
    }

    pub(crate) fn take_tracks(&self) -> Vec<Track> {
        self.tracks.lock().unwrap().drain(..).collect()
    }

    pub(crate) fn as_inner_mut(&mut self) -> &mut R {
        &mut self.repo
    }
}

impl<D, R> RepositoryStorage<D> for TrackRepository<R>
where
    R: RepositoryStorage<D> + Sync + Send,
    D: Pouf,
{
    fn store_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
        metadata: &'a mut (dyn AsyncRead + Send + Unpin),
    ) -> BoxFuture<'a, Result<()>> {
        let meta_path = meta_path.clone();
        async move {
            let mut buf = Vec::new();
            metadata.read_to_end(&mut buf).await?;

            let () = self
                .repo
                .store_metadata(&meta_path, version, &mut buf.as_slice())
                .await?;

            self.tracks
                .lock()
                .unwrap()
                .push(Track::store(&meta_path, version, buf));

            Ok(())
        }
        .boxed()
    }

    fn store_target<'a>(
        &'a self,
        target_path: &TargetPath,
        target: &'a mut (dyn AsyncRead + Send + Unpin),
    ) -> BoxFuture<'a, Result<()>> {
        self.repo.store_target(target_path, target)
    }
}

impl<D, R> RepositoryProvider<D> for TrackRepository<R>
where
    D: Pouf,
    R: RepositoryProvider<D> + Sync,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        let meta_path = meta_path.clone();
        async move {
            let fut = self.repo.fetch_metadata(&meta_path, version);
            match fut.await {
                Ok(mut rdr) => {
                    let mut buf = Vec::new();
                    rdr.read_to_end(&mut buf).await?;

                    self.tracks.lock().unwrap().push(Track::fetch_found(
                        &meta_path,
                        version,
                        buf.clone(),
                    ));

                    let rdr: Box<dyn AsyncRead + Send + Unpin> = Box::new(Cursor::new(buf));

                    Ok(rdr)
                }
                Err(err) => {
                    self.tracks
                        .lock()
                        .unwrap()
                        .push(Track::FetchErr(meta_path, version));
                    Err(err)
                }
            }
        }
        .boxed()
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &TargetPath,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        self.repo.fetch_target(target_path)
    }
}
