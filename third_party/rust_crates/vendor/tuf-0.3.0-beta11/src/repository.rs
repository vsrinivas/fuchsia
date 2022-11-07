//! Interfaces for interacting with different types of TUF repositories.

use crate::crypto::{self, HashAlgorithm, HashValue};
use crate::metadata::{
    Metadata, MetadataPath, MetadataVersion, RawSignedMetadata, TargetDescription, TargetPath,
};
use crate::pouf::Pouf;
use crate::util::SafeAsyncRead;
use crate::{Error, Result};

use futures_io::AsyncRead;
use futures_util::future::BoxFuture;
use futures_util::io::AsyncReadExt;
use std::marker::PhantomData;
use std::sync::Arc;

mod file_system;
pub use self::file_system::{
    FileSystemBatchUpdate, FileSystemRepository, FileSystemRepositoryBuilder,
};

#[cfg(feature = "hyper")]
mod http;

#[cfg(feature = "hyper")]
pub use self::http::{HttpRepository, HttpRepositoryBuilder};

mod ephemeral;
pub use self::ephemeral::{EphemeralBatchUpdate, EphemeralRepository};

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
    D: Pouf,
{
    /// Fetch signed metadata identified by `meta_path`, `version`, and
    /// [`D::extension()`][extension].
    ///
    /// Implementations may ignore `max_length` and `hash_data` as [`Client`][Client] will verify
    /// these constraints itself. However, it may be more efficient for an implementation to detect
    /// invalid metadata and fail the fetch operation before streaming all of the bytes of the
    /// metadata.
    ///
    /// [extension]: crate::pouf::Pouf::extension
    /// [Client]: crate::client::Client
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>>;

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
        target_path: &TargetPath,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>>;
}

/// Test helper to help read a metadata file from a repository into a string.
#[cfg(test)]
pub(crate) async fn fetch_metadata_to_string<D, R>(
    repo: &R,
    meta_path: &MetadataPath,
    version: MetadataVersion,
) -> Result<String>
where
    D: Pouf,
    R: RepositoryProvider<D>,
{
    let mut reader = repo.fetch_metadata(meta_path, version).await?;
    let mut buf = String::new();
    reader.read_to_string(&mut buf).await.unwrap();
    Ok(buf)
}

/// Test helper to help read a target file from a repository into a string.
#[cfg(test)]
pub(crate) async fn fetch_target_to_string<D, R>(
    repo: &R,
    target_path: &TargetPath,
) -> Result<String>
where
    D: Pouf,
    R: RepositoryProvider<D>,
{
    let mut reader = repo.fetch_target(target_path).await?;
    let mut buf = String::new();
    reader.read_to_string(&mut buf).await.unwrap();
    Ok(buf)
}

/// A writable TUF repository. Most implementors of this trait should also implement
/// `RepositoryProvider`.
pub trait RepositoryStorage<D>
where
    D: Pouf,
{
    /// Store the provided `metadata` in a location identified by `meta_path`, `version`, and
    /// [`D::extension()`][extension], overwriting any existing metadata at that location.
    ///
    /// [extension]: crate::pouf::Pouf::extension
    fn store_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
        metadata: &'a mut (dyn AsyncRead + Send + Unpin),
    ) -> BoxFuture<'a, Result<()>>;

    /// Store the provided `target` in a location identified by `target_path`, overwriting any
    /// existing target at that location.
    fn store_target<'a>(
        &'a self,
        target_path: &TargetPath,
        target: &'a mut (dyn AsyncRead + Send + Unpin),
    ) -> BoxFuture<'a, Result<()>>;
}

/// A subtrait of both RepositoryStorage and RepositoryProvider. This is useful to create
/// trait objects that implement both traits.
pub trait RepositoryStorageProvider<D>: RepositoryStorage<D> + RepositoryProvider<D>
where
    D: Pouf,
{
}

impl<D, T> RepositoryStorageProvider<D> for T
where
    D: Pouf,
    T: RepositoryStorage<D> + RepositoryProvider<D>,
{
}

macro_rules! impl_provider {
    (
        <$($desc:tt)+
    ) => {
        impl<$($desc)+ {
            fn fetch_metadata<'a>(
                &'a self,
                meta_path: &MetadataPath,
                version: MetadataVersion,
            ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
                (**self).fetch_metadata(meta_path, version)
            }

            fn fetch_target<'a>(
                &'a self,
                target_path: &TargetPath,
            ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
                (**self).fetch_target(target_path)
            }
        }
    };
}

impl_provider!(<D: Pouf, T: RepositoryProvider<D> + ?Sized> RepositoryProvider<D> for &T);
impl_provider!(<D: Pouf, T: RepositoryProvider<D> + ?Sized> RepositoryProvider<D> for &mut T);
impl_provider!(<D: Pouf, T: RepositoryProvider<D> + ?Sized> RepositoryProvider<D> for Box<T>);
impl_provider!(<D: Pouf, T: RepositoryProvider<D> + ?Sized> RepositoryProvider<D> for Arc<T>);

macro_rules! impl_storage {
    (
        <$($desc:tt)+
    ) => {
        impl<$($desc)+ {
            fn store_metadata<'a>(
                &'a self,
                meta_path: &MetadataPath,
                version: MetadataVersion,
                metadata: &'a mut (dyn AsyncRead + Send + Unpin),
            ) -> BoxFuture<'a, Result<()>> {
                (**self).store_metadata(meta_path, version, metadata)
            }

            fn store_target<'a>(
                &'a self,
                target_path: &TargetPath,
                target: &'a mut (dyn AsyncRead + Send + Unpin),
            ) -> BoxFuture<'a, Result<()>> {
                (**self).store_target(target_path, target)
            }
        }
    };
}

impl_storage!(<D: Pouf, T: RepositoryStorage<D> + ?Sized> RepositoryStorage<D> for &T);
impl_storage!(<D: Pouf, T: RepositoryStorage<D> + ?Sized> RepositoryStorage<D> for &mut T);
impl_storage!(<D: Pouf, T: RepositoryStorage<D> + ?Sized> RepositoryStorage<D> for Box<T>);
impl_storage!(<D: Pouf, T: RepositoryStorage<D> + ?Sized> RepositoryStorage<D> for Arc<T>);

/// A wrapper around an implementation of [`RepositoryProvider`] and/or [`RepositoryStorage`] tied
/// to a specific [Pouf] that will enforce provided length limits and hash checks.
#[derive(Debug, Clone)]
pub(crate) struct Repository<R, D> {
    repository: R,
    _pouf: PhantomData<D>,
}

impl<R, D> Repository<R, D> {
    /// Creates a new [`Repository`] wrapping `repository`.
    pub(crate) fn new(repository: R) -> Self {
        Self {
            repository,
            _pouf: PhantomData,
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

    pub(crate) fn into_inner(self) -> R {
        self.repository
    }

    pub(crate) fn as_inner(&self) -> &R {
        &self.repository
    }

    pub(crate) fn as_inner_mut(&mut self) -> &mut R {
        &mut self.repository
    }
}

impl<R, D> Repository<R, D>
where
    R: RepositoryProvider<D>,
    D: Pouf,
{
    /// Fetch metadata identified by `meta_path`, `version`, and [`D::extension()`][extension].
    ///
    /// If `max_length` is provided, this method will return an error if the metadata exceeds
    /// `max_length` bytes. If `hash_data` is provided, this method will return and error if the
    /// hashed bytes of the metadata do not match `hash_data`.
    ///
    /// [extension]: crate::pouf::Pouf::extension
    pub(crate) async fn fetch_metadata<'a, M>(
        &'a self,
        meta_path: &'a MetadataPath,
        version: MetadataVersion,
        max_length: Option<usize>,
        hashes: Vec<(&'static HashAlgorithm, HashValue)>,
    ) -> Result<RawSignedMetadata<D, M>>
    where
        M: Metadata,
    {
        Self::check::<M>(meta_path)?;

        // Fetch the metadata, verifying max_length and hashes (if provided), as
        // the repository implementation should only be trusted to use those as
        // hints to fail early.
        let mut reader = self
            .repository
            .fetch_metadata(meta_path, version)
            .await?
            .check_length_and_hash(max_length.unwrap_or(::std::usize::MAX) as u64, hashes)?;

        let mut buf = Vec::new();
        reader.read_to_end(&mut buf).await?;

        Ok(RawSignedMetadata::new(buf))
    }

    /// Fetch the target identified by `target_path` through the returned `AsyncRead`, verifying
    /// that the target matches the preferred hash specified in `target_description` and that it is
    /// the expected length. Such verification errors will be provided by a read failure on the
    /// provided `AsyncRead`.
    ///
    /// It is **critical** that none of the bytes from the returned `AsyncRead` are used until it
    /// has been fully consumed as the data is untrusted.
    pub(crate) async fn fetch_target(
        &self,
        consistent_snapshot: bool,
        target_path: &TargetPath,
        target_description: TargetDescription,
    ) -> Result<impl AsyncRead + Send + Unpin + '_> {
        // https://theupdateframework.github.io/specification/v1.0.26/#fetch-target 5.7.3:
        //
        // [...] download the target (up to the number of bytes specified in the targets metadata),
        // and verify that its hashes match the targets metadata.
        let length = target_description.length();
        let hashes = crypto::retain_supported_hashes(target_description.hashes());
        if hashes.is_empty() {
            return Err(Error::NoSupportedHashAlgorithm);
        }

        // https://theupdateframework.github.io/specification/v1.0.26/#fetch-target 5.7.3:
        //
        // [...] If consistent snapshots are not used (see § 6.2 Consistent snapshots), then the
        // filename used to download the target file is of the fixed form FILENAME.EXT (e.g.,
        // foobar.tar.gz). Otherwise, the filename is of the form HASH.FILENAME.EXT [...]
        let target = if consistent_snapshot {
            let mut hashes = hashes.iter();
            loop {
                if let Some((_, hash)) = hashes.next() {
                    let target_path = target_path.with_hash_prefix(hash)?;
                    match self.repository.fetch_target(&target_path).await {
                        Ok(target) => break target,
                        Err(Error::TargetNotFound(_)) => {}
                        Err(err) => return Err(err),
                    }
                } else {
                    return Err(Error::TargetNotFound(target_path.clone()));
                }
            }
        } else {
            self.repository.fetch_target(target_path).await?
        };

        target.check_length_and_hash(length, hashes)
    }
}

impl<R, D> Repository<R, D>
where
    R: RepositoryStorage<D>,
    D: Pouf,
{
    /// Store the provided `metadata` in a location identified by `meta_path`, `version`, and
    /// [`D::extension()`][extension], overwriting any existing metadata at that location.
    ///
    /// [extension]: crate::pouf::Pouf::extension
    pub async fn store_metadata<'a, M>(
        &'a mut self,
        path: &MetadataPath,
        version: MetadataVersion,
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
        &'a mut self,
        target_path: &TargetPath,
        target: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> Result<()> {
        self.repository.store_target(target_path, target).await
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::metadata::{MetadataPath, MetadataVersion, RootMetadata, SnapshotMetadata};
    use crate::pouf::Pouf1;
    use crate::repository::EphemeralRepository;
    use assert_matches::assert_matches;
    use futures_executor::block_on;

    #[test]
    fn repository_forwards_not_found_error() {
        block_on(async {
            let repo = Repository::<_, Pouf1>::new(EphemeralRepository::new());

            assert_matches!(
                repo.fetch_metadata::<RootMetadata>(
                    &MetadataPath::root(),
                    MetadataVersion::None,
                    None,
                    vec![],
                )
                .await,
                Err(Error::MetadataNotFound { path, version })
                if path == MetadataPath::root() && version == MetadataVersion::None
            );
        });
    }

    #[test]
    fn repository_rejects_mismatched_path() {
        block_on(async {
            let mut repo = Repository::<_, Pouf1>::new(EphemeralRepository::new());
            let fake_metadata = RawSignedMetadata::<Pouf1, RootMetadata>::new(vec![]);

            repo.store_metadata(&MetadataPath::root(), MetadataVersion::None, &fake_metadata)
                .await
                .unwrap();

            assert_matches!(
                repo.store_metadata(
                    &MetadataPath::snapshot(),
                    MetadataVersion::None,
                    &fake_metadata,
                )
                .await,
                Err(Error::IllegalArgument(_))
            );

            assert_matches!(
                repo.fetch_metadata::<SnapshotMetadata>(
                    &MetadataPath::root(),
                    MetadataVersion::None,
                    None,
                    vec![],
                )
                .await,
                Err(Error::IllegalArgument(_))
            );
        });
    }

    #[test]
    fn repository_verifies_metadata_hash() {
        block_on(async {
            let path = MetadataPath::root();
            let version = MetadataVersion::None;
            let data: &[u8] = b"valid metadata";
            let _metadata = RawSignedMetadata::<Pouf1, RootMetadata>::new(data.to_vec());
            let data_hash = crypto::calculate_hash(data, &HashAlgorithm::Sha256);

            let repo = EphemeralRepository::new();
            repo.store_metadata(&path, version, &mut &*data)
                .await
                .unwrap();

            let client = Repository::<_, Pouf1>::new(repo);

            assert_matches!(
                client
                    .fetch_metadata::<RootMetadata>(
                        &path,
                        version,
                        None,
                        vec![(&HashAlgorithm::Sha256, data_hash)],
                    )
                    .await,
                Ok(_metadata)
            );
        })
    }

    #[test]
    fn repository_rejects_corrupt_metadata() {
        block_on(async {
            let path = MetadataPath::root();
            let version = MetadataVersion::None;
            let data: &[u8] = b"corrupt metadata";

            let repo = EphemeralRepository::new();
            repo.store_metadata(&path, version, &mut &*data)
                .await
                .unwrap();

            let client = Repository::<_, Pouf1>::new(repo);

            assert_matches!(
                client
                    .fetch_metadata::<RootMetadata>(
                        &path,
                        version,
                        None,
                        vec![(&HashAlgorithm::Sha256, HashValue::new(vec![]))],
                    )
                    .await,
                Err(_)
            );
        })
    }

    #[test]
    fn repository_verifies_metadata_size() {
        block_on(async {
            let path = MetadataPath::root();
            let version = MetadataVersion::None;
            let data: &[u8] = b"reasonably sized metadata";
            let _metadata = RawSignedMetadata::<Pouf1, RootMetadata>::new(data.to_vec());

            let repo = EphemeralRepository::new();
            repo.store_metadata(&path, version, &mut &*data)
                .await
                .unwrap();

            let client = Repository::<_, Pouf1>::new(repo);

            assert_matches!(
                client
                    .fetch_metadata::<RootMetadata>(&path, version, Some(100), vec![])
                    .await,
                Ok(_metadata)
            );
        })
    }

    #[test]
    fn repository_rejects_oversized_metadata() {
        block_on(async {
            let path = MetadataPath::root();
            let version = MetadataVersion::None;
            let data: &[u8] = b"very big metadata";

            let repo = EphemeralRepository::new();
            repo.store_metadata(&path, version, &mut &*data)
                .await
                .unwrap();

            let client = Repository::<_, Pouf1>::new(repo);

            assert_matches!(
                client
                    .fetch_metadata::<RootMetadata>(&path, version, Some(4), vec![])
                    .await,
                Err(_)
            );
        })
    }

    #[test]
    fn repository_rejects_corrupt_targets() {
        block_on(async {
            let repo = EphemeralRepository::new();
            let mut client = Repository::<_, Pouf1>::new(repo);

            let data: &[u8] = b"like tears in the rain";
            let target_description =
                TargetDescription::from_slice(data, &[HashAlgorithm::Sha256]).unwrap();
            let path = TargetPath::new("batty").unwrap();
            client.store_target(&path, &mut &*data).await.unwrap();

            let mut read = client
                .fetch_target(false, &path, target_description.clone())
                .await
                .unwrap();
            let mut buf = Vec::new();
            read.read_to_end(&mut buf).await.unwrap();
            assert_eq!(buf.as_slice(), data);
            drop(read);

            let bad_data: &[u8] = b"you're in a desert";
            client.store_target(&path, &mut &*bad_data).await.unwrap();
            let mut read = client
                .fetch_target(false, &path, target_description)
                .await
                .unwrap();
            assert!(read.read_to_end(&mut buf).await.is_err());
        })
    }

    #[test]
    fn repository_takes_trait_objects() {
        block_on(async {
            let repo: Box<dyn RepositoryStorageProvider<Pouf1>> =
                Box::new(EphemeralRepository::new());
            let mut client = Repository::<_, Pouf1>::new(repo);

            let data: &[u8] = b"like tears in the rain";
            let target_description =
                TargetDescription::from_slice(data, &[HashAlgorithm::Sha256]).unwrap();
            let path = TargetPath::new("batty").unwrap();
            client.store_target(&path, &mut &*data).await.unwrap();

            let mut read = client
                .fetch_target(false, &path, target_description)
                .await
                .unwrap();
            let mut buf = Vec::new();
            read.read_to_end(&mut buf).await.unwrap();
            assert_eq!(buf.as_slice(), data);
        })
    }

    #[test]
    fn repository_dyn_impls_repository_traits() {
        let mut repo = EphemeralRepository::new();

        fn storage<T: RepositoryStorage<Pouf1>>(_t: T) {}
        fn provider<T: RepositoryProvider<Pouf1>>(_t: T) {}

        provider(&repo as &dyn RepositoryProvider<Pouf1>);
        provider(&mut repo as &mut dyn RepositoryProvider<Pouf1>);
        storage(&mut repo as &mut dyn RepositoryStorage<Pouf1>);
    }
}
