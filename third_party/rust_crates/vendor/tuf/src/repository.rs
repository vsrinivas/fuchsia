//! Interfaces for interacting with different types of TUF repositories.

use crate::crypto::{self, HashAlgorithm, HashValue};
use crate::interchange::DataInterchange;
use crate::metadata::{
    Metadata, MetadataPath, MetadataVersion, RawSignedMetadata, TargetDescription, TargetPath,
};
use crate::util::SafeAsyncRead;
use crate::{Error, Result};

use futures_io::AsyncRead;
use futures_util::future::BoxFuture;
use futures_util::io::AsyncReadExt;
use std::marker::PhantomData;
use std::sync::Arc;

mod file_system;
pub use self::file_system::{FileSystemRepository, FileSystemRepositoryBuilder};

mod http;
pub use self::http::{HttpRepository, HttpRepositoryBuilder};

mod ephemeral;
pub use self::ephemeral::EphemeralRepository;

#[cfg(test)]
mod error_repo;
#[cfg(test)]
pub(crate) use self::error_repo::ErrorRepository;

#[cfg(test)]
mod track_repo;
#[cfg(test)]
pub(crate) use self::track_repo::{Track, TrackRepository};

/// A readable TUF repository.
pub trait RepositoryProvider<D>
where
    D: DataInterchange + Sync,
{
    /// Fetch signed metadata identified by `meta_path`, `version`, and
    /// [`D::extension()`][extension].
    ///
    /// Implementations may ignore `max_length` and `hash_data` as [`Client`][Client] will verify
    /// these constraints itself. However, it may be more efficient for an implementation to detect
    /// invalid metadata and fail the fetch operation before streaming all of the bytes of the
    /// metadata.
    ///
    /// [extension]: crate::interchange::DataInterchange::extension
    /// [Client]: crate::client::Client
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &'a MetadataPath,
        version: &'a MetadataVersion,
        max_length: Option<usize>,
        hash_data: Option<(&'static HashAlgorithm, HashValue)>,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin>>>;

    /// Fetch the given target.
    ///
    /// Implementations may ignore the `length` and `hashes` fields in `target_description` as
    /// [`Client`][Client] will verify these constraints itself. However, it may be more efficient
    /// for an implementation to detect invalid targets and fail the fetch operation before
    /// streaming all of the bytes.
    ///
    /// [Client]: crate::client::Client
    fn fetch_target<'a>(
        &'a self,
        target_path: &'a TargetPath,
        target_description: &'a TargetDescription,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin>>>;
}

/// A writable TUF repository. Most implementors of this trait should also implement
/// `RepositoryProvider`.
pub trait RepositoryStorage<D>
where
    D: DataInterchange + Sync,
{
    /// Store the provided `metadata` in a location identified by `meta_path`, `version`, and
    /// [`D::extension()`][extension], overwriting any existing metadata at that location.
    ///
    /// [extension]: crate::interchange::DataInterchange::extension
    fn store_metadata<'a>(
        &'a self,
        meta_path: &'a MetadataPath,
        version: &'a MetadataVersion,
        metadata: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, Result<()>>;

    /// Store the provided `target` in a location identified by `target_path`, overwriting any
    /// existing target at that location.
    fn store_target<'a>(
        &'a self,
        target: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
        target_path: &'a TargetPath,
    ) -> BoxFuture<'a, Result<()>>;
}

/// A subtrait of both RepositoryStorage and RepositoryProvider. This is useful to create
/// trait objects that implement both traits.
pub trait RepositoryStorageProvider<D>: RepositoryStorage<D> + RepositoryProvider<D>
where
    D: DataInterchange + Sync,
{
}

impl<D, T> RepositoryStorageProvider<D> for T
where
    D: DataInterchange + Sync,
    T: RepositoryStorage<D> + RepositoryProvider<D>,
{
}

impl<T, D> RepositoryProvider<D> for &T
where
    T: RepositoryProvider<D>,
    D: DataInterchange + Sync,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &'a MetadataPath,
        version: &'a MetadataVersion,
        max_length: Option<usize>,
        hash_data: Option<(&'static HashAlgorithm, HashValue)>,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin>>> {
        (**self).fetch_metadata(meta_path, version, max_length, hash_data)
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &'a TargetPath,
        target_description: &'a TargetDescription,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin>>> {
        (**self).fetch_target(target_path, target_description)
    }
}

impl<T, D> RepositoryStorage<D> for &T
where
    T: RepositoryStorage<D>,
    D: DataInterchange + Sync,
{
    fn store_metadata<'a>(
        &'a self,
        meta_path: &'a MetadataPath,
        version: &'a MetadataVersion,
        metadata: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, Result<()>> {
        (**self).store_metadata(meta_path, version, metadata)
    }

    fn store_target<'a>(
        &'a self,
        target: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
        target_path: &'a TargetPath,
    ) -> BoxFuture<'a, Result<()>> {
        (**self).store_target(target, target_path)
    }
}

impl<T, D> RepositoryStorage<D> for Box<T>
where
    T: RepositoryStorage<D> + ?Sized,
    D: DataInterchange + Sync,
{
    fn store_metadata<'a>(
        &'a self,
        meta_path: &'a MetadataPath,
        version: &'a MetadataVersion,
        metadata: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, Result<()>> {
        (**self).store_metadata(meta_path, version, metadata)
    }

    fn store_target<'a>(
        &'a self,
        target: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
        target_path: &'a TargetPath,
    ) -> BoxFuture<'a, Result<()>> {
        (**self).store_target(target, target_path)
    }
}

impl<T, D> RepositoryProvider<D> for Box<T>
where
    T: RepositoryProvider<D> + ?Sized,
    D: DataInterchange + Sync,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &'a MetadataPath,
        version: &'a MetadataVersion,
        max_length: Option<usize>,
        hash_data: Option<(&'static HashAlgorithm, HashValue)>,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin>>> {
        (**self).fetch_metadata(meta_path, version, max_length, hash_data)
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &'a TargetPath,
        target_description: &'a TargetDescription,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin>>> {
        (**self).fetch_target(target_path, target_description)
    }
}

impl<D, T> RepositoryProvider<D> for Arc<T>
where
    D: DataInterchange + Sync,
    T: RepositoryProvider<D> + ?Sized,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &'a MetadataPath,
        version: &'a MetadataVersion,
        max_length: Option<usize>,
        hash_data: Option<(&'static HashAlgorithm, HashValue)>,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin>>> {
        (**self).fetch_metadata(meta_path, version, max_length, hash_data)
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &'a TargetPath,
        target_description: &'a TargetDescription,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin>>> {
        (**self).fetch_target(target_path, target_description)
    }
}

impl<T, D> RepositoryStorage<D> for Arc<T>
where
    T: RepositoryStorage<D> + ?Sized,
    D: DataInterchange + Sync,
{
    fn store_metadata<'a>(
        &'a self,
        meta_path: &'a MetadataPath,
        version: &'a MetadataVersion,
        metadata: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, Result<()>> {
        (**self).store_metadata(meta_path, version, metadata)
    }

    fn store_target<'a>(
        &'a self,
        target: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
        target_path: &'a TargetPath,
    ) -> BoxFuture<'a, Result<()>> {
        (**self).store_target(target, target_path)
    }
}

/// A wrapper around an implementation of [`RepositoryProvider`] and/or [`RepositoryStorage`] tied
/// to a specific [`DataInterchange`](crate::interchange::DataInterchange) that will enforce
/// provided length limits and hash checks.
#[derive(Debug, Clone)]
pub(crate) struct Repository<R, D> {
    repository: R,
    _interchange: PhantomData<D>,
}

impl<R, D> Repository<R, D> {
    /// Creates a new [`Repository`] wrapping `repository`.
    pub(crate) fn new(repository: R) -> Self {
        Self {
            repository,
            _interchange: PhantomData,
        }
    }

    /// Perform a sanity check that `M`, `Role`, and `MetadataPath` all describe the same entity.
    fn check<M>(meta_path: &MetadataPath) -> Result<()>
    where
        M: Metadata,
    {
        if !M::ROLE.fuzzy_matches_path(meta_path) {
            return Err(Error::IllegalArgument(format!(
                "Role {} does not match path {:?}",
                M::ROLE,
                meta_path
            )));
        }

        Ok(())
    }
}

impl<R, D> Repository<R, D>
where
    R: RepositoryProvider<D>,
    D: DataInterchange + Sync,
{
    /// Fetch metadata identified by `meta_path`, `version`, and [`D::extension()`][extension].
    ///
    /// If `max_length` is provided, this method will return an error if the metadata exceeds
    /// `max_length` bytes. If `hash_data` is provided, this method will return and error if the
    /// hashed bytes of the metadata do not match `hash_data`.
    ///
    /// [extension]: crate::interchange::DataInterchange::extension
    pub(crate) async fn fetch_metadata<'a, M>(
        &'a self,
        meta_path: &'a MetadataPath,
        version: &'a MetadataVersion,
        max_length: Option<usize>,
        hash_data: Option<(&'static HashAlgorithm, HashValue)>,
    ) -> Result<RawSignedMetadata<D, M>>
    where
        M: Metadata,
    {
        Self::check::<M>(meta_path)?;

        // Fetch the metadata, verifying max_length and hash_data if provided, as the repository
        // implementation should only be trusted to use those as hints to fail early.
        let mut reader = self
            .repository
            .fetch_metadata(meta_path, version, max_length, hash_data.clone())
            .await?
            .check_length_and_hash(max_length.unwrap_or(::std::usize::MAX) as u64, hash_data)?;

        let mut buf = Vec::new();
        reader.read_to_end(&mut buf).await?;

        let raw_signed_meta = RawSignedMetadata::new(buf);

        Ok(raw_signed_meta)
    }

    /// Fetch the target identified by `target_path` through the returned `AsyncRead`, verifying
    /// that the target matches the preferred hash specified in `target_description` and that it is
    /// the expected length. Such verification errors will be provided by a read failure on the
    /// provided `AsyncRead`.
    ///
    /// It is **critical** that none of the bytes from the returned `AsyncRead` are used until it
    /// has been fully consumed as the data is untrusted.
    pub(crate) async fn fetch_target<'a>(
        &'a self,
        target_path: &'a TargetPath,
        target_description: &'a TargetDescription,
    ) -> Result<impl AsyncRead + Send + Unpin> {
        let (hash_alg, value) = crypto::hash_preference(target_description.hashes())?;

        self.repository
            .fetch_target(target_path, target_description)
            .await?
            .check_length_and_hash(target_description.length(), Some((hash_alg, value.clone())))
    }
}

impl<R, D> Repository<R, D>
where
    R: RepositoryStorage<D>,
    D: DataInterchange + Sync,
{
    /// Store the provided `metadata` in a location identified by `meta_path`, `version`, and
    /// [`D::extension()`][extension], overwriting any existing metadata at that location.
    ///
    /// [extension]: crate::interchange::DataInterchange::extension
    pub async fn store_metadata<'a, M>(
        &'a self,
        path: &'a MetadataPath,
        version: &'a MetadataVersion,
        metadata: &'a RawSignedMetadata<D, M>,
    ) -> Result<()>
    where
        M: Metadata + Sync,
    {
        Self::check::<M>(path)?;

        self.repository
            .store_metadata(path, version, &mut metadata.as_bytes())
            .await
    }

    /// Store the provided `target` in a location identified by `target_path`.
    pub async fn store_target<'a>(
        &'a self,
        target: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
        target_path: &'a TargetPath,
    ) -> Result<()> {
        self.repository.store_target(target, target_path).await
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::interchange::Json;
    use crate::metadata::{MetadataPath, MetadataVersion, Role, RootMetadata, SnapshotMetadata};
    use crate::repository::EphemeralRepository;
    use futures_executor::block_on;
    use matches::assert_matches;

    #[test]
    fn repository_forwards_not_found_error() {
        block_on(async {
            let repo = Repository::<_, Json>::new(EphemeralRepository::new());

            assert_matches!(
                repo.fetch_metadata::<RootMetadata>(
                    &MetadataPath::from_role(&Role::Root),
                    &MetadataVersion::None,
                    None,
                    None
                )
                .await,
                Err(Error::NotFound)
            );
        });
    }

    #[test]
    fn repository_rejects_mismatched_path() {
        block_on(async {
            let repo = Repository::<_, Json>::new(EphemeralRepository::new());
            let fake_metadata = RawSignedMetadata::<Json, RootMetadata>::new(vec![]);

            repo.store_metadata(
                &MetadataPath::from_role(&Role::Root),
                &MetadataVersion::None,
                &fake_metadata,
            )
            .await
            .unwrap();

            assert_matches!(
                repo.store_metadata(
                    &MetadataPath::from_role(&Role::Snapshot),
                    &MetadataVersion::None,
                    &fake_metadata,
                )
                .await,
                Err(Error::IllegalArgument(_))
            );

            assert_matches!(
                repo.fetch_metadata::<SnapshotMetadata>(
                    &MetadataPath::from_role(&Role::Root),
                    &MetadataVersion::None,
                    None,
                    None
                )
                .await,
                Err(Error::IllegalArgument(_))
            );
        });
    }

    #[test]
    fn repository_verifies_metadata_hash() {
        block_on(async {
            let path = MetadataPath::from_role(&Role::Root);
            let version = MetadataVersion::None;
            let data: &[u8] = b"valid metadata";
            let _metadata = RawSignedMetadata::<Json, RootMetadata>::new(data.to_vec());
            let data_hash = crypto::calculate_hash(data, HashAlgorithm::Sha256);

            let repo = EphemeralRepository::new();
            repo.store_metadata(&path, &version, &mut &*data)
                .await
                .unwrap();

            let client = Repository::<_, Json>::new(repo);

            assert_matches!(
                client
                    .fetch_metadata::<RootMetadata>(
                        &path,
                        &version,
                        None,
                        Some((&HashAlgorithm::Sha256, data_hash))
                    )
                    .await,
                Ok(_metadata)
            );
        })
    }

    #[test]
    fn repository_rejects_corrupt_metadata() {
        block_on(async {
            let path = MetadataPath::from_role(&Role::Root);
            let version = MetadataVersion::None;
            let data: &[u8] = b"corrupt metadata";

            let repo = EphemeralRepository::new();
            repo.store_metadata(&path, &version, &mut &*data)
                .await
                .unwrap();

            let client = Repository::<_, Json>::new(repo);

            assert_matches!(
                client
                    .fetch_metadata::<RootMetadata>(
                        &path,
                        &version,
                        None,
                        Some((&HashAlgorithm::Sha256, HashValue::new(vec![])))
                    )
                    .await,
                Err(_)
            );
        })
    }

    #[test]
    fn repository_verifies_metadata_size() {
        block_on(async {
            let path = MetadataPath::from_role(&Role::Root);
            let version = MetadataVersion::None;
            let data: &[u8] = b"reasonably sized metadata";
            let _metadata = RawSignedMetadata::<Json, RootMetadata>::new(data.to_vec());

            let repo = EphemeralRepository::new();
            repo.store_metadata(&path, &version, &mut &*data)
                .await
                .unwrap();

            let client = Repository::<_, Json>::new(repo);

            assert_matches!(
                client
                    .fetch_metadata::<RootMetadata>(&path, &version, Some(100), None)
                    .await,
                Ok(_metadata)
            );
        })
    }

    #[test]
    fn repository_rejects_oversized_metadata() {
        block_on(async {
            let path = MetadataPath::from_role(&Role::Root);
            let version = MetadataVersion::None;
            let data: &[u8] = b"very big metadata";

            let repo = EphemeralRepository::new();
            repo.store_metadata(&path, &version, &mut &*data)
                .await
                .unwrap();

            let client = Repository::<_, Json>::new(repo);

            assert_matches!(
                client
                    .fetch_metadata::<RootMetadata>(&path, &version, Some(4), None)
                    .await,
                Err(_)
            );
        })
    }

    #[test]
    fn repository_rejects_corrupt_targets() {
        block_on(async {
            let repo = EphemeralRepository::new();
            let client = Repository::<_, Json>::new(repo);

            let data: &[u8] = b"like tears in the rain";
            let target_description =
                TargetDescription::from_reader(data, &[HashAlgorithm::Sha256]).unwrap();
            let path = TargetPath::new("batty".into()).unwrap();
            client.store_target(&mut &*data, &path).await.unwrap();

            let mut read = client
                .fetch_target(&path, &target_description)
                .await
                .unwrap();
            let mut buf = Vec::new();
            read.read_to_end(&mut buf).await.unwrap();
            assert_eq!(buf.as_slice(), data);

            let bad_data: &[u8] = b"you're in a desert";
            client.store_target(&mut &*bad_data, &path).await.unwrap();
            let mut read = client
                .fetch_target(&path, &target_description)
                .await
                .unwrap();
            assert!(read.read_to_end(&mut buf).await.is_err());
        })
    }

    #[test]
    fn repository_takes_trait_objects() {
        block_on(async {
            let repo: Box<dyn RepositoryStorageProvider<Json>> =
                Box::new(EphemeralRepository::new());
            let client = Repository::<_, Json>::new(repo);

            let data: &[u8] = b"like tears in the rain";
            let target_description =
                TargetDescription::from_reader(data, &[HashAlgorithm::Sha256]).unwrap();
            let path = TargetPath::new("batty".into()).unwrap();
            client.store_target(&mut &*data, &path).await.unwrap();

            let mut read = client
                .fetch_target(&path, &target_description)
                .await
                .unwrap();
            let mut buf = Vec::new();
            read.read_to_end(&mut buf).await.unwrap();
            assert_eq!(buf.as_slice(), data);
        })
    }

    #[test]
    fn repository_takes_arc_trait_objects() {
        block_on(async {
            let repo: Arc<dyn RepositoryStorageProvider<Json>> =
                Arc::new(EphemeralRepository::new());
            let client = Repository::<_, Json>::new(repo);

            let data: &[u8] = b"like tears in the rain";
            let target_description =
                TargetDescription::from_reader(data, &[HashAlgorithm::Sha256]).unwrap();
            let path = TargetPath::new("batty".into()).unwrap();
            client.store_target(&mut &*data, &path).await.unwrap();

            let mut read = client
                .fetch_target(&path, &target_description)
                .await
                .unwrap();
            let mut buf = Vec::new();
            read.read_to_end(&mut buf).await.unwrap();
            assert_eq!(buf.as_slice(), data);
        })
    }
}
