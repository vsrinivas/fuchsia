use {
    crate::{
        crypto::{HashAlgorithm, HashValue},
        interchange::DataInterchange,
        metadata::{
            Metadata, MetadataPath, MetadataVersion, RawSignedMetadata, TargetDescription,
            TargetPath,
        },
        repository::{RepositoryProvider, RepositoryStorage},
        Result,
    },
    futures_io::AsyncRead,
    futures_util::{
        future::{BoxFuture, FutureExt},
        io::{AsyncReadExt, Cursor},
    },
    parking_lot::Mutex,
    std::sync::Arc,
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
    pub(crate) fn store<T>(meta_path: &MetadataPath, version: &MetadataVersion, metadata: T) -> Self
    where
        T: Into<Vec<u8>>,
    {
        Track::Store {
            path: meta_path.clone(),
            version: version.clone(),
            metadata: String::from_utf8(metadata.into()).unwrap(),
        }
    }

    pub(crate) fn store_meta<M, D>(
        version: &MetadataVersion,
        metadata: &RawSignedMetadata<D, M>,
    ) -> Self
    where
        M: Metadata,
        D: DataInterchange,
    {
        Self::store(
            &MetadataPath::from_role(&M::ROLE),
            version,
            metadata.as_bytes(),
        )
    }

    pub(crate) fn fetch_found<T>(
        meta_path: &MetadataPath,
        version: &MetadataVersion,
        metadata: T,
    ) -> Self
    where
        T: Into<Vec<u8>>,
    {
        Track::FetchFound {
            path: meta_path.clone(),
            version: version.clone(),
            metadata: String::from_utf8(metadata.into()).unwrap(),
        }
    }

    pub(crate) fn fetch_meta_found<M, D>(
        version: &MetadataVersion,
        metadata: &RawSignedMetadata<D, M>,
    ) -> Self
    where
        M: Metadata,
        D: DataInterchange,
    {
        Track::fetch_found(
            &MetadataPath::from_role(&M::ROLE),
            version,
            metadata.as_bytes(),
        )
    }
}

/// Helper Repository wrapper that tracks all the metadata fetches and stores for testing purposes.
pub(crate) struct TrackRepository<R> {
    repo: R,
    tracks: Arc<Mutex<Vec<Track>>>,
}

impl<R> TrackRepository<R> {
    pub(crate) fn new(repo: R) -> Self {
        Self {
            repo,
            tracks: Arc::new(Mutex::new(vec![])),
        }
    }

    pub(crate) fn take_tracks(&self) -> Vec<Track> {
        self.tracks.lock().drain(..).collect()
    }
}

impl<D, R> RepositoryStorage<D> for TrackRepository<R>
where
    R: RepositoryStorage<D> + Sync + Send,
    D: DataInterchange + Sync,
{
    fn store_metadata<'a>(
        &'a self,
        meta_path: &'a MetadataPath,
        version: &'a MetadataVersion,
        metadata: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, Result<()>> {
        async move {
            let mut buf = Vec::new();
            metadata.read_to_end(&mut buf).await?;

            let () = self
                .repo
                .store_metadata(meta_path, version, &mut buf.as_slice())
                .await?;

            self.tracks
                .lock()
                .push(Track::store(meta_path, version, buf));

            Ok(())
        }
        .boxed()
    }

    fn store_target<'a>(
        &'a self,
        target: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
        target_path: &'a TargetPath,
    ) -> BoxFuture<'a, Result<()>> {
        self.repo.store_target(target, target_path)
    }
}

impl<D, R> RepositoryProvider<D> for TrackRepository<R>
where
    D: DataInterchange + Sync,
    R: RepositoryProvider<D> + Sync,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &'a MetadataPath,
        version: &'a MetadataVersion,
        max_length: Option<usize>,
        hash_data: Option<(&'static HashAlgorithm, HashValue)>,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin>>> {
        async move {
            let fut = self
                .repo
                .fetch_metadata(meta_path, version, max_length, hash_data);
            match fut.await {
                Ok(mut rdr) => {
                    let mut buf = Vec::new();
                    rdr.read_to_end(&mut buf).await?;

                    self.tracks
                        .lock()
                        .push(Track::fetch_found(meta_path, version, buf.clone()));

                    let rdr: Box<dyn AsyncRead + Send + Unpin> = Box::new(Cursor::new(buf));

                    Ok(rdr)
                }
                Err(err) => {
                    self.tracks
                        .lock()
                        .push(Track::FetchErr(meta_path.clone(), version.clone()));
                    Err(err)
                }
            }
        }
        .boxed()
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &'a TargetPath,
        target_description: &'a TargetDescription,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin>>> {
        self.repo.fetch_target(target_path, target_description)
    }
}
