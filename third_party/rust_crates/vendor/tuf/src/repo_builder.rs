//! Repository Builder

use {
    crate::{
        crypto::{self, HashAlgorithm, PrivateKey, PublicKey},
        database::Database,
        error::{Error, Result},
        metadata::{
            Delegation, DelegationsBuilder, Metadata, MetadataDescription, MetadataPath,
            MetadataVersion, RawSignedMetadata, RawSignedMetadataSet, RawSignedMetadataSetBuilder,
            RootMetadata, RootMetadataBuilder, SignedMetadataBuilder, SnapshotMetadata,
            SnapshotMetadataBuilder, TargetDescription, TargetPath, TargetsMetadata,
            TargetsMetadataBuilder, TimestampMetadata, TimestampMetadataBuilder,
        },
        pouf::Pouf,
        repository::RepositoryStorage,
        verify::Verified,
    },
    chrono::{DateTime, Duration, Utc},
    futures_io::{AsyncRead, AsyncSeek},
    futures_util::AsyncSeekExt as _,
    std::{collections::HashMap, io::SeekFrom, marker::PhantomData},
};

mod private {
    use super::*;

    /// Implement the [sealed] pattern to make public traits that cannot be externally modified.
    ///
    /// [sealed]: https://rust-lang.github.io/api-guidelines/future-proofing.html#sealed-traits-protect-against-downstream-implementations-c-sealed
    pub trait Sealed {}

    impl Sealed for Root {}
    impl<D: Pouf> Sealed for Targets<D> {}
    impl<D: Pouf> Sealed for Snapshot<D> {}
    impl<D: Pouf> Sealed for Timestamp<D> {}
    impl<D: Pouf> Sealed for Done<D> {}
}

const DEFAULT_ROOT_EXPIRATION_DAYS: i64 = 365;
const DEFAULT_TARGETS_EXPIRATION_DAYS: i64 = 90;
const DEFAULT_SNAPSHOT_EXPIRATION_DAYS: i64 = 7;
const DEFAULT_TIMESTAMP_EXPIRATION_DAYS: i64 = 1;

/// Trait to track each of the [RepoBuilder] building states.
///
/// This trait is [sealed] to make
/// sure external users cannot implement the `State` crate with unexpected states.
///
/// [sealed]: https://rust-lang.github.io/api-guidelines/future-proofing.html#sealed-traits-protect-against-downstream-implementations-c-sealed
pub trait State: private::Sealed {}

/// State to stage a root metadata.
#[doc(hidden)]
pub struct Root {
    builder: RootMetadataBuilder,
}

impl State for Root {}

/// State to stage a targets metadata.
#[doc(hidden)]
pub struct Targets<D: Pouf> {
    staged_root: Option<Staged<D, RootMetadata>>,
    targets: HashMap<TargetPath, TargetDescription>,
    delegation_keys: Vec<PublicKey>,
    delegation_roles: Vec<Delegation>,
    file_hash_algorithms: Vec<HashAlgorithm>,
    inherit_from_trusted_targets: bool,
}

impl<D: Pouf> Targets<D> {
    fn new(staged_root: Option<Staged<D, RootMetadata>>) -> Self {
        Self {
            staged_root,
            targets: HashMap::new(),
            delegation_keys: vec![],
            delegation_roles: vec![],
            file_hash_algorithms: vec![HashAlgorithm::Sha256],
            inherit_from_trusted_targets: true,
        }
    }
}

impl<D: Pouf> State for Targets<D> {}

/// State to stage a snapshot metadata.
#[doc(hidden)]
pub struct Snapshot<D: Pouf> {
    staged_root: Option<Staged<D, RootMetadata>>,
    staged_targets: Option<Staged<D, TargetsMetadata>>,
    include_targets_length: bool,
    targets_hash_algorithms: Vec<HashAlgorithm>,
    inherit_from_trusted_snapshot: bool,
}

impl<D: Pouf> State for Snapshot<D> {}

impl<D: Pouf> Snapshot<D> {
    fn new(
        staged_root: Option<Staged<D, RootMetadata>>,
        staged_targets: Option<Staged<D, TargetsMetadata>>,
    ) -> Self {
        Self {
            staged_root,
            staged_targets,
            include_targets_length: false,
            targets_hash_algorithms: vec![],
            inherit_from_trusted_snapshot: true,
        }
    }

    fn targets_description(&self) -> Result<Option<MetadataDescription<TargetsMetadata>>> {
        if let Some(ref targets) = self.staged_targets {
            let length = if self.include_targets_length {
                Some(targets.raw.as_bytes().len())
            } else {
                None
            };

            let hashes = if self.targets_hash_algorithms.is_empty() {
                HashMap::new()
            } else {
                crypto::calculate_hashes_from_slice(
                    targets.raw.as_bytes(),
                    &self.targets_hash_algorithms,
                )?
            };

            Ok(Some(MetadataDescription::new(
                targets.metadata.version(),
                length,
                hashes,
            )?))
        } else {
            Ok(None)
        }
    }
}

/// State to stage a timestamp metadata.
pub struct Timestamp<D: Pouf> {
    staged_root: Option<Staged<D, RootMetadata>>,
    staged_targets: Option<Staged<D, TargetsMetadata>>,
    staged_snapshot: Option<Staged<D, SnapshotMetadata>>,
    include_snapshot_length: bool,
    snapshot_hash_algorithms: Vec<HashAlgorithm>,
}

impl<D: Pouf> Timestamp<D> {
    fn new(state: Snapshot<D>, staged_snapshot: Option<Staged<D, SnapshotMetadata>>) -> Self {
        Self {
            staged_root: state.staged_root,
            staged_targets: state.staged_targets,
            staged_snapshot,
            include_snapshot_length: false,
            snapshot_hash_algorithms: vec![],
        }
    }

    fn snapshot_description(&self) -> Result<Option<MetadataDescription<SnapshotMetadata>>> {
        if let Some(ref snapshot) = self.staged_snapshot {
            let length = if self.include_snapshot_length {
                Some(snapshot.raw.as_bytes().len())
            } else {
                None
            };

            let hashes = if self.snapshot_hash_algorithms.is_empty() {
                HashMap::new()
            } else {
                crypto::calculate_hashes_from_slice(
                    snapshot.raw.as_bytes(),
                    &self.snapshot_hash_algorithms,
                )?
            };

            Ok(Some(MetadataDescription::new(
                snapshot.metadata.version(),
                length,
                hashes,
            )?))
        } else {
            Ok(None)
        }
    }
}

impl<D: Pouf> State for Timestamp<D> {}

/// The final state for building repository metadata.
pub struct Done<D: Pouf> {
    staged_root: Option<Staged<D, RootMetadata>>,
    staged_targets: Option<Staged<D, TargetsMetadata>>,
    staged_snapshot: Option<Staged<D, SnapshotMetadata>>,
    staged_timestamp: Option<Staged<D, TimestampMetadata>>,
}

impl<D: Pouf> State for Done<D> {}

struct Staged<D: Pouf, M: Metadata> {
    metadata: M,
    raw: RawSignedMetadata<D, M>,
}

struct RepoContext<'a, D, R>
where
    D: Pouf,
    R: RepositoryStorage<D>,
{
    repo: R,
    db: Option<&'a Database<D>>,
    current_time: DateTime<Utc>,
    signing_root_keys: Vec<&'a dyn PrivateKey>,
    signing_targets_keys: Vec<&'a dyn PrivateKey>,
    signing_snapshot_keys: Vec<&'a dyn PrivateKey>,
    signing_timestamp_keys: Vec<&'a dyn PrivateKey>,
    trusted_root_keys: Vec<&'a dyn PrivateKey>,
    trusted_targets_keys: Vec<&'a dyn PrivateKey>,
    trusted_snapshot_keys: Vec<&'a dyn PrivateKey>,
    trusted_timestamp_keys: Vec<&'a dyn PrivateKey>,
    time_version: Option<u32>,
    root_expiration_duration: Duration,
    targets_expiration_duration: Duration,
    snapshot_expiration_duration: Duration,
    timestamp_expiration_duration: Duration,
    _pouf: PhantomData<D>,
}

impl<'a, D, R> RepoContext<'a, D, R>
where
    D: Pouf,
    R: RepositoryStorage<D>,
{
    fn root_keys_changed(&self, root: &Verified<RootMetadata>) -> bool {
        let root_keys_count = root.root_keys().count();
        if root_keys_count != self.trusted_root_keys.len() {
            return true;
        }

        for key in &self.trusted_root_keys {
            if root.root().key_ids().get(key.public().key_id()).is_none() {
                return true;
            }
        }

        false
    }
    fn targets_keys_changed(&self, root: &Verified<RootMetadata>) -> bool {
        for key in &self.trusted_targets_keys {
            if root
                .targets()
                .key_ids()
                .get(key.public().key_id())
                .is_none()
            {
                return true;
            }
        }

        false
    }

    fn snapshot_keys_changed(&self, root: &Verified<RootMetadata>) -> bool {
        for key in &self.trusted_snapshot_keys {
            if root
                .snapshot()
                .key_ids()
                .get(key.public().key_id())
                .is_none()
            {
                return true;
            }
        }

        false
    }

    fn timestamp_keys_changed(&self, root: &Verified<RootMetadata>) -> bool {
        for key in &self.trusted_timestamp_keys {
            if root
                .timestamp()
                .key_ids()
                .get(key.public().key_id())
                .is_none()
            {
                return true;
            }
        }

        false
    }

    /// The initial version number for non-root metadata.
    fn non_root_initial_version(&self) -> u32 {
        if let Some(time_version) = self.time_version {
            time_version
        } else {
            1
        }
    }

    /// If time versioning is enabled, this updates the current time version to match the current
    /// time. It will disable time versioning if the current timestamp is less than or equal to
    /// zero, or it is greater than max u32.
    fn update_time_version(&mut self) {
        // We can use the time version if it is greater than zero and less than max u32. Otherwise
        // fall back to default monontonic versioning.
        let timestamp = self.current_time.timestamp();
        if timestamp > 0 {
            self.time_version = timestamp.try_into().ok();
        } else {
            self.time_version = None;
        }
    }

    /// The next version number for non-root metadata.
    fn non_root_next_version(
        &self,
        current_version: u32,
        path: fn() -> MetadataPath,
    ) -> Result<u32> {
        if let Some(time_version) = self.time_version {
            // We can only use the time version if it's larger than our current version. If not,
            // then fall back to the next version.
            if current_version < time_version {
                return Ok(time_version);
            }
        }

        current_version
            .checked_add(1)
            .ok_or_else(|| Error::MetadataVersionMustBeSmallerThanMaxU32(path()))
    }
}

fn sign<'a, D, I, M>(meta: &M, keys: I) -> Result<RawSignedMetadata<D, M>>
where
    D: Pouf,
    M: Metadata,
    I: IntoIterator<Item = &'a &'a dyn PrivateKey>,
{
    // Sign the root.
    let mut signed_builder = SignedMetadataBuilder::<D, _>::from_metadata(meta)?;
    let mut has_key = false;
    for key in keys {
        has_key = true;
        signed_builder = signed_builder.sign(*key)?;
    }

    // We need at least one private key to sign the metadata.
    if !has_key {
        return Err(Error::MissingPrivateKey {
            role: M::ROLE.into(),
        });
    }

    signed_builder.build().to_raw()
}

/// This helper builder simplifies the process of creating new metadata.
pub struct RepoBuilder<'a, D, R, S = Root>
where
    D: Pouf,
    R: RepositoryStorage<D>,
    S: State,
{
    ctx: RepoContext<'a, D, R>,
    state: S,
}

impl<'a, D, R> RepoBuilder<'a, D, R, Root>
where
    D: Pouf,
    R: RepositoryStorage<D>,
{
    /// Create a [RepoBuilder] for creating metadata for a new repository.
    ///
    /// # Examples
    ///
    /// ```rust
    /// # use {
    /// #     futures_executor::block_on,
    /// #     tuf::{
    /// #         pouf::Pouf1,
    /// #         crypto::Ed25519PrivateKey,
    /// #         repo_builder::RepoBuilder,
    /// #         repository::EphemeralRepository,
    /// #     },
    /// # };
    /// #
    /// # let key = Ed25519PrivateKey::from_pkcs8(
    /// #     include_bytes!("../tests/ed25519/ed25519-1.pk8.der")
    /// # ).unwrap();
    /// #
    /// # block_on(async {
    /// let mut repo = EphemeralRepository::<Pouf1>::new();
    /// let _metadata = RepoBuilder::create(&mut repo)
    ///     .trusted_root_keys(&[&key])
    ///     .trusted_targets_keys(&[&key])
    ///     .trusted_snapshot_keys(&[&key])
    ///     .trusted_timestamp_keys(&[&key])
    ///     .commit()
    ///     .await
    ///     .unwrap();
    /// # });
    /// ```
    pub fn create(repo: R) -> Self {
        Self {
            ctx: RepoContext {
                repo,
                db: None,
                current_time: Utc::now(),
                signing_root_keys: vec![],
                signing_targets_keys: vec![],
                signing_snapshot_keys: vec![],
                signing_timestamp_keys: vec![],
                trusted_root_keys: vec![],
                trusted_targets_keys: vec![],
                trusted_snapshot_keys: vec![],
                trusted_timestamp_keys: vec![],
                time_version: None,
                root_expiration_duration: Duration::days(DEFAULT_ROOT_EXPIRATION_DAYS),
                targets_expiration_duration: Duration::days(DEFAULT_TARGETS_EXPIRATION_DAYS),
                snapshot_expiration_duration: Duration::days(DEFAULT_SNAPSHOT_EXPIRATION_DAYS),
                timestamp_expiration_duration: Duration::days(DEFAULT_TIMESTAMP_EXPIRATION_DAYS),
                _pouf: PhantomData,
            },
            state: Root {
                builder: RootMetadataBuilder::new()
                    .consistent_snapshot(true)
                    .root_threshold(1)
                    .targets_threshold(1)
                    .snapshot_threshold(1)
                    .timestamp_threshold(1),
            },
        }
    }

    /// Create a [RepoBuilder] for creating metadata based off the latest metadata in the
    /// [Database].
    ///
    /// # Examples
    ///
    /// ```rust
    /// # use {
    /// #     futures_executor::block_on,
    /// #     tuf::{
    /// #         database::Database,
    /// #         crypto::Ed25519PrivateKey,
    /// #         pouf::Pouf1,
    /// #         repo_builder::RepoBuilder,
    /// #         repository::EphemeralRepository,
    /// #     },
    /// # };
    /// #
    /// # let key = Ed25519PrivateKey::from_pkcs8(
    /// #     include_bytes!("../tests/ed25519/ed25519-1.pk8.der")
    /// # ).unwrap();
    /// #
    /// # block_on(async {
    ///  let mut repo = EphemeralRepository::<Pouf1>::new();
    ///  let metadata1 = RepoBuilder::create(&mut repo)
    ///     .trusted_root_keys(&[&key])
    ///     .trusted_targets_keys(&[&key])
    ///     .trusted_snapshot_keys(&[&key])
    ///     .trusted_timestamp_keys(&[&key])
    ///     .commit()
    ///     .await
    ///     .unwrap();
    ///
    /// let database = Database::from_trusted_metadata(&metadata1).unwrap();
    ///
    /// let _metadata2 = RepoBuilder::from_database(&mut repo, &database)
    ///     .trusted_root_keys(&[&key])
    ///     .trusted_targets_keys(&[&key])
    ///     .trusted_snapshot_keys(&[&key])
    ///     .trusted_timestamp_keys(&[&key])
    ///     .stage_root()
    ///     .unwrap()
    ///     .commit()
    ///     .await
    ///     .unwrap();
    /// # });
    /// ```
    pub fn from_database(repo: R, db: &'a Database<D>) -> Self {
        let builder = {
            let trusted_root = db.trusted_root();

            RootMetadataBuilder::new()
                .consistent_snapshot(trusted_root.consistent_snapshot())
                .root_threshold(trusted_root.root().threshold())
                .targets_threshold(trusted_root.targets().threshold())
                .snapshot_threshold(trusted_root.snapshot().threshold())
                .timestamp_threshold(trusted_root.timestamp().threshold())
        };

        Self {
            ctx: RepoContext {
                repo,
                db: Some(db),
                current_time: Utc::now(),
                signing_root_keys: vec![],
                signing_targets_keys: vec![],
                signing_snapshot_keys: vec![],
                signing_timestamp_keys: vec![],
                trusted_root_keys: vec![],
                trusted_targets_keys: vec![],
                trusted_snapshot_keys: vec![],
                trusted_timestamp_keys: vec![],
                time_version: None,
                root_expiration_duration: Duration::days(DEFAULT_ROOT_EXPIRATION_DAYS),
                targets_expiration_duration: Duration::days(DEFAULT_TARGETS_EXPIRATION_DAYS),
                snapshot_expiration_duration: Duration::days(DEFAULT_SNAPSHOT_EXPIRATION_DAYS),
                timestamp_expiration_duration: Duration::days(DEFAULT_TIMESTAMP_EXPIRATION_DAYS),
                _pouf: PhantomData,
            },
            state: Root { builder },
        }
    }

    /// Change the time the builder will use to see if metadata is expired, and the base time to use
    /// to compute the next expiration.
    ///
    /// Default is the current wall clock time in UTC.
    pub fn current_time(mut self, current_time: DateTime<Utc>) -> Self {
        self.ctx.current_time = current_time;

        // Update our time version if enabled.
        if self.ctx.time_version.is_some() {
            self.ctx.update_time_version();
        }

        self
    }

    /// Create Non-root metadata based off the current UTC timestamp, instead of a monotonic
    /// increment.
    pub fn time_versioning(mut self, time_versioning: bool) -> Self {
        if time_versioning {
            self.ctx.update_time_version();
        } else {
            self.ctx.time_version = None;
        }
        self
    }

    /// Sets that the root metadata will expire after this duration past the current time.
    ///
    /// Defaults to 365 days.
    ///
    /// Note: calling this function will only change what is the metadata expiration we'll use if we
    /// create a new root metadata if we call [RepoBuilder::stage_root], or we decide a new one is
    /// needed when we call [RepoBuilder::stage_root_if_necessary].
    pub fn root_expiration_duration(mut self, duration: Duration) -> Self {
        self.ctx.root_expiration_duration = duration;
        self
    }

    /// Sets that the targets metadata will expire after after this duration past the current time.
    ///
    /// Defaults to 90 days.
    ///
    /// Note: calling this function will only change what is the metadata expiration we'll use if we
    /// create a new targets metadata if we call [RepoBuilder::stage_targets], or we decide a new
    /// one is needed when we call [RepoBuilder::stage_targets_if_necessary].
    pub fn targets_expiration_duration(mut self, duration: Duration) -> Self {
        self.ctx.targets_expiration_duration = duration;
        self
    }

    /// Sets that the snapshot metadata will expire after after this duration past the current time.
    ///
    /// Defaults to 7 days.
    ///
    /// Note: calling this function will only change what is the metadata expiration we'll use if we
    /// create a new snapshot metadata if we call [RepoBuilder::stage_snapshot], or we decide a new
    /// one is needed when we call [RepoBuilder::stage_snapshot_if_necessary].
    pub fn snapshot_expiration_duration(mut self, duration: Duration) -> Self {
        self.ctx.snapshot_expiration_duration = duration;
        self
    }

    /// Sets that the timestamp metadata will expire after after this duration past the current
    /// time.
    ///
    /// Defaults to 1 day.
    ///
    /// Note: calling this function will only change what is the metadata expiration we'll use if we
    /// create a new timestamp metadata if we call [RepoBuilder::stage_timestamp], or we decide a
    /// new one is needed when we call [RepoBuilder::stage_timestamp_if_necessary].
    pub fn timestamp_expiration_duration(mut self, duration: Duration) -> Self {
        self.ctx.timestamp_expiration_duration = duration;
        self
    }

    /// Sign the root metadata with `keys`, but do not include the keys as trusted root keys in the
    /// root metadata. This is typically used to support root key rotation.
    pub fn signing_root_keys(mut self, keys: &[&'a dyn PrivateKey]) -> Self {
        for key in keys {
            self.ctx.signing_root_keys.push(*key);
        }
        self
    }

    /// Sign the targets metadata with `keys`, but do not include the keys as trusted targets keys
    /// in the root metadata. This is typically used to support targets key rotation.
    pub fn signing_targets_keys(mut self, keys: &[&'a dyn PrivateKey]) -> Self {
        for key in keys {
            self.ctx.signing_targets_keys.push(*key);
        }
        self
    }

    /// Sign the snapshot metadata with `keys`, but do not include the keys as trusted snapshot keys
    /// in the root metadata. This is typically used to support snapshot key rotation.
    pub fn signing_snapshot_keys(mut self, keys: &[&'a dyn PrivateKey]) -> Self {
        for key in keys {
            self.ctx.signing_snapshot_keys.push(*key);
        }
        self
    }

    /// Sign the timestamp metadata with `keys`, but do not include the keys as trusted timestamp
    /// keys in the root metadata. This is typically used to support timestamp key rotation.
    pub fn signing_timestamp_keys(mut self, keys: &[&'a dyn PrivateKey]) -> Self {
        for key in keys {
            self.ctx.signing_timestamp_keys.push(*key);
        }
        self
    }

    /// Sign the root metadata with `keys`, and include the keys as trusted root keys in the root
    /// metadata.
    pub fn trusted_root_keys(mut self, keys: &[&'a dyn PrivateKey]) -> Self {
        for key in keys {
            self.ctx.trusted_root_keys.push(*key);
            self.state.builder = self.state.builder.root_key(key.public().clone());
        }
        self
    }

    /// Sign the targets metadata with `keys`, and include the keys as trusted targets keys in the
    /// targets metadata.
    pub fn trusted_targets_keys(mut self, keys: &[&'a dyn PrivateKey]) -> Self {
        for key in keys {
            self.ctx.trusted_targets_keys.push(*key);
            self.state.builder = self.state.builder.targets_key(key.public().clone());
        }
        self
    }

    /// Sign the snapshot metadata with `keys`, and include the keys as trusted snapshot keys in the
    /// root metadata.
    pub fn trusted_snapshot_keys(mut self, keys: &[&'a dyn PrivateKey]) -> Self {
        for key in keys {
            self.ctx.trusted_snapshot_keys.push(*key);
            self.state.builder = self.state.builder.snapshot_key(key.public().clone());
        }
        self
    }

    /// Sign the timestamp metadata with `keys`, and include the keys as
    /// trusted timestamp keys in the root metadata.
    pub fn trusted_timestamp_keys(mut self, keys: &[&'a dyn PrivateKey]) -> Self {
        for key in keys {
            self.ctx.trusted_timestamp_keys.push(*key);
            self.state.builder = self.state.builder.timestamp_key(key.public().clone());
        }
        self
    }

    /// Stage a root metadata.
    ///
    /// If this is a new repository, the root will be staged with:
    ///
    /// * version: 1
    /// * consistent_snapshot: true
    /// * expires: 365 days from the current day.
    /// * root_threshold: 1
    /// * targets_threshold: 1
    /// * snapshot_threshold: 1
    /// * timestamp_threshold: 1
    ///
    /// Otherwise, it will be staged with:
    ///
    /// * version: 1 after the trusted root's version
    /// * expires: 365 days from the current day.
    /// * consistent_snapshot: match the trusted root's consistent snapshot
    /// * root_threshold: match the trusted root's root threshold
    /// * targets_threshold: match the trusted root's targets threshold
    /// * snapshot_threshold: match the trusted root's snapshot threshold
    /// * timestamp_threshold: match the trusted root's timestamp threshold
    pub fn stage_root(self) -> Result<RepoBuilder<'a, D, R, Targets<D>>> {
        self.stage_root_with_builder(|builder| builder)
    }

    /// Stage a new root using the default settings if:
    ///
    /// * There is no trusted root metadata.
    /// * The trusted keys are different from the keys that are in the trusted root.
    /// * The trusted root metadata has expired.
    pub fn stage_root_if_necessary(self) -> Result<RepoBuilder<'a, D, R, Targets<D>>> {
        if self.need_new_root() {
            self.stage_root()
        } else {
            Ok(self.skip_root())
        }
    }

    /// Skip creating the root metadata. This may cause [commit](#method.commit-4) to fail if this
    /// is a new repository.
    pub fn skip_root(self) -> RepoBuilder<'a, D, R, Targets<D>> {
        RepoBuilder {
            ctx: self.ctx,
            state: Targets::new(None),
        }
    }

    /// Initialize a [RootMetadataBuilder] and pass it to the closure for further configuration.
    /// This builder will then be used to generate and stage a new [RootMetadata] for eventual
    /// commitment to the repository.
    ///
    /// If this is a new repository, the builder will be initialized with the following defaults:
    ///
    /// * version: 1
    /// * consistent_snapshot: true
    /// * expires: 365 days from the current day.
    /// * root_threshold: 1
    /// * targets_threshold: 1
    /// * snapshot_threshold: 1
    /// * timestamp_threshold: 1
    ///
    /// Otherwise, it will be initialized with:
    ///
    /// * version: 1 after the trusted root's version
    /// * expires: 365 days from the current day.
    /// * consistent_snapshot: match the trusted root's consistent snapshot
    /// * root_threshold: match the trusted root's root threshold
    /// * targets_threshold: match the trusted root's targets threshold
    /// * snapshot_threshold: match the trusted root's snapshot threshold
    /// * timestamp_threshold: match the trusted root's timestamp threshold
    pub fn stage_root_with_builder<F>(self, f: F) -> Result<RepoBuilder<'a, D, R, Targets<D>>>
    where
        F: FnOnce(RootMetadataBuilder) -> RootMetadataBuilder,
    {
        let next_version = if let Some(db) = self.ctx.db {
            db.trusted_root().version().checked_add(1).ok_or_else(|| {
                Error::MetadataVersionMustBeSmallerThanMaxU32(MetadataPath::root())
            })?
        } else {
            1
        };

        let root_builder = self
            .state
            .builder
            .version(next_version)
            .expires(self.ctx.current_time + self.ctx.root_expiration_duration);
        let root = f(root_builder).build()?;

        let raw_root = sign(
            &root,
            self.ctx
                .signing_root_keys
                .iter()
                .chain(&self.ctx.trusted_root_keys),
        )?;

        Ok(RepoBuilder {
            ctx: self.ctx,
            state: Targets::new(Some(Staged {
                metadata: root,
                raw: raw_root,
            })),
        })
    }

    /// Add a target that's loaded in from the reader. This will store the target in the repository,
    /// and may stage a root metadata if necessary.
    ///
    /// This will hash the file with [HashAlgorithm::Sha256].
    ///
    /// See [RepoBuilder<Targets>::add_target] for more details.
    pub async fn add_target<Rd>(
        self,
        target_path: TargetPath,
        reader: Rd,
    ) -> Result<RepoBuilder<'a, D, R, Targets<D>>>
    where
        Rd: AsyncRead + AsyncSeek + Unpin + Send,
    {
        self.stage_root_if_necessary()?
            .add_target(target_path, reader)
            .await
    }

    /// Validate and write the metadata to the repository.
    ///
    /// This may stage a root, targets, snapshot, and timestamp metadata if necessary.
    ///
    /// See [RepoBuilder::commit] for more details.
    pub async fn commit(self) -> Result<RawSignedMetadataSet<D>> {
        self.stage_root_if_necessary()?.commit().await
    }

    /// Check if we need a new root database.
    fn need_new_root(&self) -> bool {
        // We need a new root metadata if we don't have a database yet.
        let trusted_root = if let Some(db) = self.ctx.db {
            db.trusted_root()
        } else {
            return true;
        };

        // We need a new root metadata if the metadata expired.
        if trusted_root.expires() <= &self.ctx.current_time {
            return true;
        }

        // Sign the metadata if we passed in any old root keys.
        if !self.ctx.signing_root_keys.is_empty() {
            return true;
        }

        // Otherwise, see if any of the keys have changed.
        self.ctx.root_keys_changed(trusted_root)
            || self.ctx.targets_keys_changed(trusted_root)
            || self.ctx.snapshot_keys_changed(trusted_root)
            || self.ctx.timestamp_keys_changed(trusted_root)
    }
}

impl<'a, D, R> RepoBuilder<'a, D, R, Targets<D>>
where
    D: Pouf,
    R: RepositoryStorage<D>,
{
    /// Whether or not to include the length of the targets, and any delegated targets, in the
    /// new snapshot.
    ///
    /// Default is `[HashAlgorithm::Sha256]`.
    pub fn target_hash_algorithms(mut self, algorithms: &[HashAlgorithm]) -> Self {
        self.state.file_hash_algorithms = algorithms.to_vec();
        self
    }

    /// Whether or not the new targets metadata inherits targets and delegations from the trusted
    /// targets metadata.
    ///
    /// Default is `true`.
    pub fn inherit_from_trusted_targets(mut self, inherit: bool) -> Self {
        self.state.inherit_from_trusted_targets = inherit;
        self
    }

    /// Stage a targets metadata using the default settings.
    pub fn stage_targets(self) -> Result<RepoBuilder<'a, D, R, Snapshot<D>>> {
        self.stage_targets_with_builder(|builder| builder)
    }

    /// Stage a new targets using the default settings if:
    ///
    /// * There is no trusted targets metadata.
    /// * The trusted targets metadata has expired.
    pub fn stage_targets_if_necessary(self) -> Result<RepoBuilder<'a, D, R, Snapshot<D>>> {
        if self.need_new_targets() {
            self.stage_targets_with_builder(|builder| builder)
        } else {
            Ok(self.skip_targets())
        }
    }

    /// Skip creating the targets metadata.
    pub fn skip_targets(self) -> RepoBuilder<'a, D, R, Snapshot<D>> {
        RepoBuilder {
            ctx: self.ctx,
            state: Snapshot::new(self.state.staged_root, None),
        }
    }

    /// Add a target that's loaded in from the reader. This will store the target in the repository.
    ///
    /// This will hash the file with the hash specified in [RepoBuilder::target_hash_algorithms]. If
    /// none was specified, the file will be hashed with [HashAlgorithm::Sha256].
    pub async fn add_target<Rd>(
        self,
        target_path: TargetPath,
        reader: Rd,
    ) -> Result<RepoBuilder<'a, D, R, Targets<D>>>
    where
        Rd: AsyncRead + AsyncSeek + Unpin + Send,
    {
        self.add_target_with_custom(target_path, reader, HashMap::new())
            .await
    }

    /// Add a target that's loaded in from the reader. This will store the target in the repository.
    ///
    /// This will hash the file with the hash specified in [RepoBuilder::target_hash_algorithms]. If
    /// none was specified, the file will be hashed with [HashAlgorithm::Sha256].
    pub async fn add_target_with_custom<Rd>(
        mut self,
        target_path: TargetPath,
        mut reader: Rd,
        custom: HashMap<String, serde_json::Value>,
    ) -> Result<RepoBuilder<'a, D, R, Targets<D>>>
    where
        Rd: AsyncRead + AsyncSeek + Unpin + Send,
    {
        let consistent_snapshot = if let Some(ref staged_root) = self.state.staged_root {
            staged_root.metadata.consistent_snapshot()
        } else if let Some(db) = self.ctx.db {
            db.trusted_root().consistent_snapshot()
        } else {
            return Err(Error::MetadataNotFound {
                path: MetadataPath::root(),
                version: MetadataVersion::None,
            });
        };

        let target_description = TargetDescription::from_reader_with_custom(
            &mut reader,
            &self.state.file_hash_algorithms,
            custom,
        )
        .await?;

        // According to TUF section 5.5.2, when consistent snapshot is enabled, target files should be
        // stored at `$HASH.FILENAME.EXT`. Otherwise it is stored at `FILENAME.EXT`.
        if consistent_snapshot {
            for digest in target_description.hashes().values() {
                reader.seek(SeekFrom::Start(0)).await?;

                let hash_prefixed_path = target_path.with_hash_prefix(digest)?;

                self.ctx
                    .repo
                    .store_target(&hash_prefixed_path, &mut reader)
                    .await?;
            }
        } else {
            reader.seek(SeekFrom::Start(0)).await?;

            self.ctx
                .repo
                .store_target(&target_path, &mut reader)
                .await?;
        }

        self.state.targets.insert(target_path, target_description);

        Ok(self)
    }

    /// Add a target delegation key.
    pub fn add_delegation_key(mut self, key: PublicKey) -> Self {
        self.state.delegation_keys.push(key);
        self
    }

    /// Add a target delegation role.
    pub fn add_delegation_role(mut self, delegation: Delegation) -> Self {
        self.state.delegation_roles.push(delegation);
        self
    }

    /// Initialize a [TargetsMetadataBuilder] and pass it to the closure for further configuration.
    /// This builder will then be used to generate and stage a new [TargetsMetadata] for eventual
    /// commitment to the repository.
    ///
    /// This builder will be initialized with:
    ///
    /// * version: 1 if a new repository, otherwise 1 past the trusted targets's version.
    /// * expires: 90 days from the current day.
    pub fn stage_targets_with_builder<F>(self, f: F) -> Result<RepoBuilder<'a, D, R, Snapshot<D>>>
    where
        F: FnOnce(TargetsMetadataBuilder) -> TargetsMetadataBuilder,
    {
        let mut targets_builder = TargetsMetadataBuilder::new()
            .expires(self.ctx.current_time + self.ctx.targets_expiration_duration);

        let mut delegations_builder = DelegationsBuilder::new();

        if let Some(trusted_targets) = self.ctx.db.and_then(|db| db.trusted_targets()) {
            let next_version = self
                .ctx
                .non_root_next_version(trusted_targets.version(), MetadataPath::targets)?;

            targets_builder = targets_builder.version(next_version);

            // Insert all the metadata from the trusted snapshot.
            if self.state.inherit_from_trusted_targets {
                for (target_path, target_description) in trusted_targets.targets() {
                    targets_builder = targets_builder
                        .insert_target_description(target_path.clone(), target_description.clone());
                }

                for key in trusted_targets.delegations().keys().values() {
                    delegations_builder = delegations_builder.key(key.clone());
                }

                for role in trusted_targets.delegations().roles() {
                    delegations_builder = delegations_builder.role(role.clone());
                }
            }
        } else {
            targets_builder = targets_builder.version(self.ctx.non_root_initial_version());
        }

        // Overwrite any of the old targets with the new ones.
        for (target_path, target_description) in self.state.targets {
            targets_builder = targets_builder
                .insert_target_description(target_path.clone(), target_description.clone());
        }

        // Overwrite the old delegation keys.
        for key in self.state.delegation_keys {
            delegations_builder = delegations_builder.key(key);
        }

        // Overwrite the old delegation roles.
        for role in self.state.delegation_roles {
            delegations_builder = delegations_builder.role(role);
        }

        targets_builder = targets_builder.delegations(delegations_builder.build()?);

        let targets = f(targets_builder).build()?;

        // Sign the targets metadata.
        let raw_targets = sign(
            &targets,
            self.ctx
                .signing_targets_keys
                .iter()
                .chain(&self.ctx.trusted_targets_keys),
        )?;

        Ok(RepoBuilder {
            ctx: self.ctx,
            state: Snapshot::new(
                self.state.staged_root,
                Some(Staged {
                    metadata: targets,
                    raw: raw_targets,
                }),
            ),
        })
    }

    /// Validate and write the metadata to the repository.
    ///
    /// This may stage a targets, snapshot, and timestamp metadata if necessary.
    ///
    /// See [RepoBuilder::commit](#method.commit-4) for more details.
    pub async fn commit(self) -> Result<RawSignedMetadataSet<D>> {
        self.stage_targets_if_necessary()?.commit().await
    }

    fn need_new_targets(&self) -> bool {
        // We need a new targets metadata if we added any targets.
        if !self.state.targets.is_empty() {
            return true;
        }

        // We need a new targets metadata if we staged a new root.
        if self.state.staged_root.is_some() {
            return true;
        }

        // We need a new targets metadata if we don't have a database yet.
        let db = if let Some(ref db) = self.ctx.db {
            db
        } else {
            return true;
        };

        // We need a new targets metadata if the database doesn't have a targets.
        let trusted_targets = if let Some(trusted_targets) = db.trusted_targets() {
            trusted_targets
        } else {
            return true;
        };

        // We need a new targets metadata if the metadata expired.
        if trusted_targets.expires() <= &self.ctx.current_time {
            return true;
        }

        // Otherwise, see if the targets keys have changed.
        self.ctx.targets_keys_changed(db.trusted_root())
    }
}

impl<'a, D, R> RepoBuilder<'a, D, R, Snapshot<D>>
where
    D: Pouf,
    R: RepositoryStorage<D>,
{
    /// Whether or not to include the length of the targets, and any delegated targets, in the
    /// new snapshot.
    ///
    /// Default is `false`.
    pub fn snapshot_includes_length(mut self, include_targets_lengths: bool) -> Self {
        self.state.include_targets_length = include_targets_lengths;
        self
    }

    /// Whether or not to include the hashes of the targets, and any delegated targets, in the
    /// new snapshot.
    ///
    /// Default is `&[]`.
    pub fn snapshot_includes_hashes(mut self, hashes: &[HashAlgorithm]) -> Self {
        self.state.targets_hash_algorithms = hashes.to_vec();
        self
    }

    /// Whether or not the new snapshot to inherit metafiles from the trusted snapshot.
    ///
    /// Default is `true`.
    pub fn inherit_from_trusted_snapshot(mut self, inherit: bool) -> Self {
        self.state.inherit_from_trusted_snapshot = inherit;
        self
    }

    /// Stage a snapshot metadata using the default settings.
    pub fn stage_snapshot(self) -> Result<RepoBuilder<'a, D, R, Timestamp<D>>> {
        self.stage_snapshot_with_builder(|builder| builder)
    }

    /// Stage a new snapshot using the default settings if:
    ///
    /// * There is no trusted snapshot metadata.
    /// * The trusted snapshot metadata has expired.
    pub fn stage_snapshot_if_necessary(self) -> Result<RepoBuilder<'a, D, R, Timestamp<D>>> {
        if self.need_new_snapshot() {
            self.stage_snapshot()
        } else {
            Ok(self.skip_snapshot())
        }
    }

    /// Skip creating the snapshot metadata.
    pub fn skip_snapshot(self) -> RepoBuilder<'a, D, R, Timestamp<D>> {
        RepoBuilder {
            ctx: self.ctx,
            state: Timestamp::new(self.state, None),
        }
    }

    /// Initialize a [SnapshotMetadataBuilder] and pass it to the closure for further configuration.
    /// This builder will then be used to generate and stage a new [SnapshotMetadata] for eventual
    /// commitment to the repository.
    ///
    /// This builder will be initialized with:
    ///
    /// * version: 1 if a new repository, otherwise 1 past the trusted snapshot's version.
    /// * expires: 7 days from the current day.
    pub fn stage_snapshot_with_builder<F>(self, f: F) -> Result<RepoBuilder<'a, D, R, Timestamp<D>>>
    where
        F: FnOnce(SnapshotMetadataBuilder) -> SnapshotMetadataBuilder,
    {
        let mut snapshot_builder = SnapshotMetadataBuilder::new()
            .expires(self.ctx.current_time + self.ctx.snapshot_expiration_duration);

        if let Some(trusted_snapshot) = self.ctx.db.and_then(|db| db.trusted_snapshot()) {
            let next_version = self
                .ctx
                .non_root_next_version(trusted_snapshot.version(), MetadataPath::snapshot)?;

            snapshot_builder = snapshot_builder.version(next_version);

            // Insert all the metadata from the trusted snapshot.
            if self.state.inherit_from_trusted_snapshot {
                for (path, description) in trusted_snapshot.meta() {
                    snapshot_builder = snapshot_builder
                        .insert_metadata_description(path.clone(), description.clone());
                }
            }
        } else {
            snapshot_builder = snapshot_builder.version(self.ctx.non_root_initial_version());
        }

        // Overwrite the targets entry if specified.
        if let Some(targets_description) = self.state.targets_description()? {
            snapshot_builder = snapshot_builder
                .insert_metadata_description(MetadataPath::targets(), targets_description);
        };

        let snapshot = f(snapshot_builder).build()?;
        let raw_snapshot = sign(
            &snapshot,
            self.ctx
                .signing_snapshot_keys
                .iter()
                .chain(&self.ctx.trusted_snapshot_keys),
        )?;

        Ok(RepoBuilder {
            ctx: self.ctx,
            state: Timestamp::new(
                self.state,
                Some(Staged {
                    metadata: snapshot,
                    raw: raw_snapshot,
                }),
            ),
        })
    }

    /// Validate and write the metadata to the repository.
    ///
    /// This may stage a snapshot and timestamp metadata if necessary.
    ///
    /// See [RepoBuilder::commit](#method.commit-4) for more details.
    pub async fn commit(self) -> Result<RawSignedMetadataSet<D>> {
        self.stage_snapshot_if_necessary()?.commit().await
    }

    fn need_new_snapshot(&self) -> bool {
        // We need a new snapshot metadata if we staged a new root.
        if self.state.staged_root.is_some() {
            return true;
        }

        // We need a new snapshot metadata if we staged a new targets.
        if self.state.staged_targets.is_some() {
            return true;
        }

        // We need a new snapshot metadata if we don't have a database yet.
        let db = if let Some(ref db) = self.ctx.db {
            db
        } else {
            return true;
        };

        // We need a new snapshot metadata if the database doesn't have a snapshot.
        let trusted_snapshot = if let Some(trusted_snapshot) = db.trusted_snapshot() {
            trusted_snapshot
        } else {
            return true;
        };

        // We need a new snapshot metadata if the metadata expired.
        if trusted_snapshot.expires() <= &self.ctx.current_time {
            return true;
        }

        // Otherwise, see if the snapshot keys have changed.
        self.ctx.snapshot_keys_changed(db.trusted_root())
    }
}

impl<'a, D, R> RepoBuilder<'a, D, R, Timestamp<D>>
where
    D: Pouf,
    R: RepositoryStorage<D>,
{
    /// Whether or not to include the length of the snapshot, and any delegated snapshot, in the
    /// new snapshot.
    pub fn timestamp_includes_length(mut self, include_snapshot_lengths: bool) -> Self {
        self.state.include_snapshot_length = include_snapshot_lengths;
        self
    }

    /// Whether or not to include the hashes of the snapshot in the
    /// new timestamp.
    pub fn timestamp_includes_hashes(mut self, hashes: &[HashAlgorithm]) -> Self {
        self.state.snapshot_hash_algorithms = hashes.to_vec();
        self
    }

    /// Stage a timestamp metadata using the default settings.
    ///
    /// Note: This will also:
    /// * stage a root metadata with the default settings if necessary.
    /// * stage a targets metadata if necessary.
    /// * stage a snapshot metadata if necessary.
    pub fn stage_timestamp(self) -> Result<RepoBuilder<'a, D, R, Done<D>>> {
        self.stage_timestamp_with_builder(|builder| builder)
    }

    /// Stage a new timestamp using the default settings if:
    ///
    /// * There is no trusted timestamp metadata.
    /// * The trusted timestamp metadata has expired.
    pub fn stage_timestamp_if_necessary(self) -> Result<RepoBuilder<'a, D, R, Done<D>>> {
        if self.need_new_timestamp() {
            self.stage_timestamp()
        } else {
            Ok(self.skip_timestamp())
        }
    }

    /// Skip creating the timestamp metadata.
    pub fn skip_timestamp(self) -> RepoBuilder<'a, D, R, Done<D>> {
        RepoBuilder {
            ctx: self.ctx,
            state: Done {
                staged_root: self.state.staged_root,
                staged_targets: self.state.staged_targets,
                staged_snapshot: self.state.staged_snapshot,
                staged_timestamp: None,
            },
        }
    }

    /// Initialize a [TimestampMetadataBuilder] and pass it to the closure for further configuration.
    /// This builder will then be used to generate and stage a new [TimestampMetadata] for eventual
    /// commitment to the repository.
    ///
    /// This builder will be initialized with:
    ///
    /// * version: 1 if a new repository, otherwise 1 past the trusted snapshot's version.
    /// * expires: 1 day from the current day.
    pub fn stage_timestamp_with_builder<F>(self, f: F) -> Result<RepoBuilder<'a, D, R, Done<D>>>
    where
        F: FnOnce(TimestampMetadataBuilder) -> TimestampMetadataBuilder,
    {
        let next_version = if let Some(db) = self.ctx.db {
            if let Some(trusted_timestamp) = db.trusted_timestamp() {
                self.ctx
                    .non_root_next_version(trusted_timestamp.version(), MetadataPath::timestamp)?
            } else {
                self.ctx.non_root_initial_version()
            }
        } else {
            self.ctx.non_root_initial_version()
        };

        let description = if let Some(description) = self.state.snapshot_description()? {
            description
        } else {
            self.ctx
                .db
                .and_then(|db| db.trusted_timestamp())
                .map(|timestamp| timestamp.snapshot().clone())
                .ok_or_else(|| Error::MetadataNotFound {
                    path: MetadataPath::snapshot(),
                    version: MetadataVersion::None,
                })?
        };

        let timestamp_builder = TimestampMetadataBuilder::from_metadata_description(description)
            .version(next_version)
            .expires(self.ctx.current_time + self.ctx.timestamp_expiration_duration);

        let timestamp = f(timestamp_builder).build()?;
        let raw_timestamp = sign(
            &timestamp,
            self.ctx
                .signing_timestamp_keys
                .iter()
                .chain(&self.ctx.trusted_timestamp_keys),
        )?;

        Ok(RepoBuilder {
            ctx: self.ctx,
            state: Done {
                staged_root: self.state.staged_root,
                staged_targets: self.state.staged_targets,
                staged_snapshot: self.state.staged_snapshot,
                staged_timestamp: Some(Staged {
                    metadata: timestamp,
                    raw: raw_timestamp,
                }),
            },
        })
    }

    /// See [RepoBuilder::commit](#method.commit-4) for more details.
    pub async fn commit(self) -> Result<RawSignedMetadataSet<D>> {
        self.stage_timestamp_if_necessary()?.commit().await
    }

    fn need_new_timestamp(&self) -> bool {
        // We need a new timestamp metadata if we staged a new root.
        if self.state.staged_root.is_some() {
            return true;
        }

        // We need a new timestamp metadata if we staged a new snapshot.
        if self.state.staged_snapshot.is_some() {
            return true;
        }

        // We need a new timestamp metadata if we don't have a database yet.
        let db = if let Some(ref db) = self.ctx.db {
            db
        } else {
            return true;
        };

        // We need a new timestamp metadata if the database doesn't have a timestamp.
        let trusted_timestamp = if let Some(trusted_timestamp) = db.trusted_timestamp() {
            trusted_timestamp
        } else {
            return true;
        };

        // We need a new timestamp metadata if the metadata expired.
        if trusted_timestamp.expires() <= &self.ctx.current_time {
            return true;
        }

        // Otherwise, see if the timestamp keys have changed.
        self.ctx.timestamp_keys_changed(db.trusted_root())
    }
}

impl<'a, D, R> RepoBuilder<'a, D, R, Done<D>>
where
    D: Pouf,
    R: RepositoryStorage<D>,
{
    /// Commit the metadata for this repository, then write all metadata to the repository. Before
    /// writing the metadata to `repo`, this will test that a client can update to this metadata to
    /// make sure it is valid.
    pub async fn commit(mut self) -> Result<RawSignedMetadataSet<D>> {
        self.validate_built_metadata()?;
        self.write_repo().await?;

        let mut builder = RawSignedMetadataSetBuilder::new();

        if let Some(root) = self.state.staged_root {
            builder = builder.root(root.raw);
        }

        if let Some(targets) = self.state.staged_targets {
            builder = builder.targets(targets.raw);
        }

        if let Some(snapshot) = self.state.staged_snapshot {
            builder = builder.snapshot(snapshot.raw);
        }

        if let Some(timestamp) = self.state.staged_timestamp {
            builder = builder.timestamp(timestamp.raw);
        }

        Ok(builder.build())
    }

    /// Before we commit any metadata, make sure that we can update from our
    /// current TUF database to the latest version.
    fn validate_built_metadata(&self) -> Result<()> {
        // Use a TUF database to make sure we can update to the metadata we just
        // produced. If we were constructed with a database, create a copy of it
        // and make sure we can install the update.
        let mut db = if let Some(db) = self.ctx.db {
            let mut db = db.clone();

            if let Some(ref root) = self.state.staged_root {
                db.update_root(&root.raw)?;
            }

            db
        } else if let Some(ref root) = self.state.staged_root {
            Database::from_trusted_root(&root.raw)?
        } else {
            return Err(Error::MetadataNotFound {
                path: MetadataPath::root(),
                version: MetadataVersion::None,
            });
        };

        if let Some(ref timestamp) = self.state.staged_timestamp {
            db.update_timestamp(&self.ctx.current_time, &timestamp.raw)?;
        }

        if let Some(ref snapshot) = self.state.staged_snapshot {
            db.update_snapshot(&self.ctx.current_time, &snapshot.raw)?;
        }

        if let Some(ref targets) = self.state.staged_targets {
            db.update_targets(&self.ctx.current_time, &targets.raw)?;
        }

        Ok(())
    }

    async fn write_repo(&mut self) -> Result<()> {
        let consistent_snapshot = if let Some(ref root) = self.state.staged_root {
            self.ctx
                .repo
                .store_metadata(
                    &MetadataPath::root(),
                    MetadataVersion::Number(root.metadata.version()),
                    &mut root.raw.as_bytes(),
                )
                .await?;

            self.ctx
                .repo
                .store_metadata(
                    &MetadataPath::root(),
                    MetadataVersion::None,
                    &mut root.raw.as_bytes(),
                )
                .await?;

            root.metadata.consistent_snapshot()
        } else if let Some(db) = self.ctx.db {
            db.trusted_root().consistent_snapshot()
        } else {
            return Err(Error::MetadataNotFound {
                path: MetadataPath::root(),
                version: MetadataVersion::None,
            });
        };

        if let Some(ref targets) = self.state.staged_targets {
            let path = MetadataPath::targets();
            self.ctx
                .repo
                .store_metadata(
                    &path.clone(),
                    MetadataVersion::None,
                    &mut targets.raw.as_bytes(),
                )
                .await?;

            if consistent_snapshot {
                self.ctx
                    .repo
                    .store_metadata(
                        &path,
                        MetadataVersion::Number(targets.metadata.version()),
                        &mut targets.raw.as_bytes(),
                    )
                    .await?;
            }
        }

        if let Some(ref snapshot) = self.state.staged_snapshot {
            let path = MetadataPath::snapshot();
            self.ctx
                .repo
                .store_metadata(&path, MetadataVersion::None, &mut snapshot.raw.as_bytes())
                .await?;

            if consistent_snapshot {
                self.ctx
                    .repo
                    .store_metadata(
                        &path,
                        MetadataVersion::Number(snapshot.metadata.version()),
                        &mut snapshot.raw.as_bytes(),
                    )
                    .await?;
            }
        }

        if let Some(ref timestamp) = self.state.staged_timestamp {
            self.ctx
                .repo
                .store_metadata(
                    &MetadataPath::timestamp(),
                    MetadataVersion::None,
                    &mut timestamp.raw.as_bytes(),
                )
                .await?;
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            client::{Client, Config},
            crypto::Ed25519PrivateKey,
            metadata::SignedMetadata,
            pouf::Pouf1,
            repository::{EphemeralRepository, RepositoryProvider},
        },
        assert_matches::assert_matches,
        chrono::{
            offset::{TimeZone as _, Utc},
            DateTime,
        },
        futures_executor::block_on,
        futures_util::io::{AsyncReadExt, Cursor},
        lazy_static::lazy_static,
        maplit::hashmap,
        pretty_assertions::assert_eq,
        std::collections::BTreeMap,
    };

    lazy_static! {
        static ref KEYS: Vec<Ed25519PrivateKey> = {
            let keys: &[&[u8]] = &[
                include_bytes!("../tests/ed25519/ed25519-1.pk8.der"),
                include_bytes!("../tests/ed25519/ed25519-2.pk8.der"),
                include_bytes!("../tests/ed25519/ed25519-3.pk8.der"),
                include_bytes!("../tests/ed25519/ed25519-4.pk8.der"),
                include_bytes!("../tests/ed25519/ed25519-5.pk8.der"),
                include_bytes!("../tests/ed25519/ed25519-6.pk8.der"),
            ];
            keys.iter()
                .map(|b| Ed25519PrivateKey::from_pkcs8(b).unwrap())
                .collect()
        };
    }

    fn create_root(
        version: u32,
        consistent_snapshot: bool,
        expires: DateTime<Utc>,
    ) -> SignedMetadata<Pouf1, RootMetadata> {
        let root = RootMetadataBuilder::new()
            .version(version)
            .consistent_snapshot(consistent_snapshot)
            .expires(expires)
            .root_threshold(2)
            .root_key(KEYS[0].public().clone())
            .root_key(KEYS[1].public().clone())
            .root_key(KEYS[2].public().clone())
            .targets_threshold(2)
            .targets_key(KEYS[1].public().clone())
            .targets_key(KEYS[2].public().clone())
            .targets_key(KEYS[3].public().clone())
            .snapshot_threshold(2)
            .snapshot_key(KEYS[2].public().clone())
            .snapshot_key(KEYS[3].public().clone())
            .snapshot_key(KEYS[4].public().clone())
            .timestamp_threshold(2)
            .timestamp_key(KEYS[3].public().clone())
            .timestamp_key(KEYS[4].public().clone())
            .timestamp_key(KEYS[5].public().clone())
            .build()
            .unwrap();

        SignedMetadataBuilder::from_metadata(&root)
            .unwrap()
            .sign(&KEYS[0])
            .unwrap()
            .sign(&KEYS[1])
            .unwrap()
            .sign(&KEYS[2])
            .unwrap()
            .build()
    }

    fn create_targets(
        version: u32,
        expires: DateTime<Utc>,
    ) -> SignedMetadata<Pouf1, TargetsMetadata> {
        let targets = TargetsMetadataBuilder::new()
            .version(version)
            .expires(expires)
            .build()
            .unwrap();
        SignedMetadataBuilder::<Pouf1, _>::from_metadata(&targets)
            .unwrap()
            .sign(&KEYS[1])
            .unwrap()
            .sign(&KEYS[2])
            .unwrap()
            .sign(&KEYS[3])
            .unwrap()
            .build()
    }

    fn create_snapshot(
        version: u32,
        expires: DateTime<Utc>,
        targets: &SignedMetadata<Pouf1, TargetsMetadata>,
        include_length_and_hashes: bool,
    ) -> SignedMetadata<Pouf1, SnapshotMetadata> {
        let description = if include_length_and_hashes {
            let raw_targets = targets.to_raw().unwrap();
            let hashes = crypto::calculate_hashes_from_slice(
                raw_targets.as_bytes(),
                &[HashAlgorithm::Sha256],
            )
            .unwrap();

            MetadataDescription::new(version, Some(raw_targets.as_bytes().len()), hashes).unwrap()
        } else {
            MetadataDescription::new(version, None, HashMap::new()).unwrap()
        };

        let snapshot = SnapshotMetadataBuilder::new()
            .insert_metadata_description(MetadataPath::targets(), description)
            .version(version)
            .expires(expires)
            .build()
            .unwrap();
        SignedMetadataBuilder::<Pouf1, _>::from_metadata(&snapshot)
            .unwrap()
            .sign(&KEYS[2])
            .unwrap()
            .sign(&KEYS[3])
            .unwrap()
            .sign(&KEYS[4])
            .unwrap()
            .build()
    }

    fn create_timestamp(
        version: u32,
        expires: DateTime<Utc>,
        snapshot: &SignedMetadata<Pouf1, SnapshotMetadata>,
        include_length_and_hashes: bool,
    ) -> SignedMetadata<Pouf1, TimestampMetadata> {
        let description = if include_length_and_hashes {
            let raw_snapshot = snapshot.to_raw().unwrap();
            let hashes = crypto::calculate_hashes_from_slice(
                raw_snapshot.as_bytes(),
                &[HashAlgorithm::Sha256],
            )
            .unwrap();

            MetadataDescription::new(version, Some(raw_snapshot.as_bytes().len()), hashes).unwrap()
        } else {
            MetadataDescription::new(version, None, HashMap::new()).unwrap()
        };

        let timestamp = TimestampMetadataBuilder::from_metadata_description(description)
            .version(version)
            .expires(expires)
            .build()
            .unwrap();
        SignedMetadataBuilder::<Pouf1, _>::from_metadata(&timestamp)
            .unwrap()
            .sign(&KEYS[3])
            .unwrap()
            .sign(&KEYS[4])
            .unwrap()
            .sign(&KEYS[5])
            .unwrap()
            .build()
    }

    fn assert_metadata(
        metadata: &RawSignedMetadataSet<Pouf1>,
        expected_root: Option<&RawSignedMetadata<Pouf1, RootMetadata>>,
        expected_targets: Option<&RawSignedMetadata<Pouf1, TargetsMetadata>>,
        expected_snapshot: Option<&RawSignedMetadata<Pouf1, SnapshotMetadata>>,
        expected_timestamp: Option<&RawSignedMetadata<Pouf1, TimestampMetadata>>,
    ) {
        assert_eq!(
            metadata.root().map(|m| m.parse_untrusted().unwrap()),
            expected_root.map(|m| m.parse_untrusted().unwrap())
        );
        assert_eq!(
            metadata.targets().map(|m| m.parse_untrusted().unwrap()),
            expected_targets.map(|m| m.parse_untrusted().unwrap())
        );
        assert_eq!(
            metadata.snapshot().map(|m| m.parse_untrusted().unwrap()),
            expected_snapshot.map(|m| m.parse_untrusted().unwrap())
        );
        assert_eq!(
            metadata.timestamp().map(|m| m.parse_untrusted().unwrap()),
            expected_timestamp.map(|m| m.parse_untrusted().unwrap())
        );
    }

    fn assert_repo(
        repo: &EphemeralRepository<Pouf1>,
        expected_metadata: &BTreeMap<(MetadataPath, MetadataVersion), &[u8]>,
    ) {
        let actual_metadata = repo
            .metadata()
            .iter()
            .map(|(k, v)| (k.clone(), String::from_utf8_lossy(v).to_string()))
            .collect::<BTreeMap<_, _>>();

        let expected_metadata = expected_metadata
            .iter()
            .map(|(k, v)| (k.clone(), String::from_utf8_lossy(v).to_string()))
            .collect::<BTreeMap<_, _>>();

        assert_eq!(
            actual_metadata.keys().collect::<Vec<_>>(),
            expected_metadata.keys().collect::<Vec<_>>()
        );
        assert_eq!(actual_metadata, expected_metadata);
    }

    #[test]
    fn test_stage_and_update_repo_not_consistent_snapshot() {
        block_on(check_stage_and_update_repo(false));
    }

    #[test]
    fn test_stage_and_update_repo_consistent_snapshot() {
        block_on(check_stage_and_update_repo(true));
    }

    async fn check_stage_and_update_repo(consistent_snapshot: bool) {
        // We'll write all the metadata to this remote repository.
        let mut remote = EphemeralRepository::<Pouf1>::new();

        // First, create the metadata.
        let expires1 = Utc.ymd(2038, 1, 1).and_hms(0, 0, 0);
        let metadata1 = RepoBuilder::create(&mut remote)
            .trusted_root_keys(&[&KEYS[0], &KEYS[1], &KEYS[2]])
            .trusted_targets_keys(&[&KEYS[1], &KEYS[2], &KEYS[3]])
            .trusted_snapshot_keys(&[&KEYS[2], &KEYS[3], &KEYS[4]])
            .trusted_timestamp_keys(&[&KEYS[3], &KEYS[4], &KEYS[5]])
            .stage_root_with_builder(|builder| {
                builder
                    .expires(expires1)
                    .consistent_snapshot(consistent_snapshot)
                    .root_threshold(2)
                    .targets_threshold(2)
                    .snapshot_threshold(2)
                    .timestamp_threshold(2)
            })
            .unwrap()
            .stage_targets_with_builder(|builder| builder.expires(expires1))
            .unwrap()
            .snapshot_includes_length(true)
            .snapshot_includes_hashes(&[HashAlgorithm::Sha256])
            .stage_snapshot_with_builder(|builder| builder.expires(expires1))
            .unwrap()
            .timestamp_includes_length(true)
            .timestamp_includes_hashes(&[HashAlgorithm::Sha256])
            .stage_timestamp_with_builder(|builder| builder.expires(expires1))
            .unwrap()
            .commit()
            .await
            .unwrap();

        // Generate the expected metadata by hand, and make sure we produced
        // what we expected.
        let signed_root1 = create_root(1, consistent_snapshot, expires1);
        let signed_targets1 = create_targets(1, expires1);
        let signed_snapshot1 = create_snapshot(1, expires1, &signed_targets1, true);
        let signed_timestamp1 = create_timestamp(1, expires1, &signed_snapshot1, true);

        let raw_root1 = signed_root1.to_raw().unwrap();
        let raw_targets1 = signed_targets1.to_raw().unwrap();
        let raw_snapshot1 = signed_snapshot1.to_raw().unwrap();
        let raw_timestamp1 = signed_timestamp1.to_raw().unwrap();

        assert_metadata(
            &metadata1,
            Some(&raw_root1),
            Some(&raw_targets1),
            Some(&raw_snapshot1),
            Some(&raw_timestamp1),
        );

        // Make sure we stored the metadata correctly.
        let mut expected_metadata: BTreeMap<_, _> = vec![
            (
                (MetadataPath::root(), MetadataVersion::Number(1)),
                raw_root1.as_bytes(),
            ),
            (
                (MetadataPath::root(), MetadataVersion::None),
                raw_root1.as_bytes(),
            ),
            (
                (MetadataPath::targets(), MetadataVersion::None),
                raw_targets1.as_bytes(),
            ),
            (
                (MetadataPath::snapshot(), MetadataVersion::None),
                raw_snapshot1.as_bytes(),
            ),
            (
                (MetadataPath::timestamp(), MetadataVersion::None),
                raw_timestamp1.as_bytes(),
            ),
        ]
        .into_iter()
        .collect();

        if consistent_snapshot {
            expected_metadata.extend(vec![
                (
                    (MetadataPath::targets(), MetadataVersion::Number(1)),
                    raw_targets1.as_bytes(),
                ),
                (
                    (MetadataPath::snapshot(), MetadataVersion::Number(1)),
                    raw_snapshot1.as_bytes(),
                ),
            ]);
        }

        assert_repo(&remote, &expected_metadata);

        // Create a client, and make sure we can update to the version we
        // just made.
        let mut client = Client::with_trusted_root(
            Config::default(),
            metadata1.root().unwrap(),
            EphemeralRepository::new(),
            remote,
        )
        .await
        .unwrap();
        client.update().await.unwrap();
        assert_eq!(client.database().trusted_root().version(), 1);
        assert_eq!(
            client.database().trusted_targets().map(|m| m.version()),
            Some(1)
        );
        assert_eq!(
            client.database().trusted_snapshot().map(|m| m.version()),
            Some(1)
        );
        assert_eq!(
            client.database().trusted_timestamp().map(|m| m.version()),
            Some(1)
        );

        // Create a new metadata, derived from the tuf database we created
        // with the client.
        let expires2 = Utc.ymd(2038, 1, 2).and_hms(0, 0, 0);
        let mut parts = client.into_parts();
        let metadata2 = RepoBuilder::from_database(&mut parts.remote, &parts.database)
            .trusted_root_keys(&[&KEYS[0], &KEYS[1], &KEYS[2]])
            .trusted_targets_keys(&[&KEYS[1], &KEYS[2], &KEYS[3]])
            .trusted_snapshot_keys(&[&KEYS[2], &KEYS[3], &KEYS[4]])
            .trusted_timestamp_keys(&[&KEYS[3], &KEYS[4], &KEYS[5]])
            .stage_root_with_builder(|builder| builder.expires(expires2))
            .unwrap()
            .stage_targets_with_builder(|builder| builder.expires(expires2))
            .unwrap()
            .snapshot_includes_length(false)
            .snapshot_includes_hashes(&[])
            .stage_snapshot_with_builder(|builder| builder.expires(expires2))
            .unwrap()
            .timestamp_includes_length(false)
            .timestamp_includes_hashes(&[])
            .stage_timestamp_with_builder(|builder| builder.expires(expires2))
            .unwrap()
            .commit()
            .await
            .unwrap();

        // Make sure the new metadata was generated as expected.
        let signed_root2 = create_root(2, consistent_snapshot, expires2);
        let signed_targets2 = create_targets(2, expires2);
        let signed_snapshot2 = create_snapshot(2, expires2, &signed_targets2, false);
        let signed_timestamp2 = create_timestamp(2, expires2, &signed_snapshot2, false);

        let raw_root2 = signed_root2.to_raw().unwrap();
        let raw_targets2 = signed_targets2.to_raw().unwrap();
        let raw_snapshot2 = signed_snapshot2.to_raw().unwrap();
        let raw_timestamp2 = signed_timestamp2.to_raw().unwrap();

        assert_metadata(
            &metadata2,
            Some(&raw_root2),
            Some(&raw_targets2),
            Some(&raw_snapshot2),
            Some(&raw_timestamp2),
        );

        // Check that the new metadata was written.
        expected_metadata.extend(vec![
            (
                (MetadataPath::root(), MetadataVersion::Number(2)),
                raw_root2.as_bytes(),
            ),
            (
                (MetadataPath::root(), MetadataVersion::None),
                raw_root2.as_bytes(),
            ),
            (
                (MetadataPath::targets(), MetadataVersion::None),
                raw_targets2.as_bytes(),
            ),
            (
                (MetadataPath::snapshot(), MetadataVersion::None),
                raw_snapshot2.as_bytes(),
            ),
            (
                (MetadataPath::timestamp(), MetadataVersion::None),
                raw_timestamp2.as_bytes(),
            ),
        ]);

        if consistent_snapshot {
            expected_metadata.extend(vec![
                (
                    (MetadataPath::targets(), MetadataVersion::Number(2)),
                    raw_targets2.as_bytes(),
                ),
                (
                    (MetadataPath::snapshot(), MetadataVersion::Number(2)),
                    raw_snapshot2.as_bytes(),
                ),
            ]);
        }

        assert_repo(&parts.remote, &expected_metadata);

        // And make sure the client can update to the latest metadata.
        let mut client = Client::from_parts(parts);
        client.update().await.unwrap();
        assert_eq!(client.database().trusted_root().version(), 2);
        assert_eq!(
            client.database().trusted_targets().map(|m| m.version()),
            Some(2)
        );
        assert_eq!(
            client.database().trusted_snapshot().map(|m| m.version()),
            Some(2)
        );
        assert_eq!(
            client.database().trusted_timestamp().map(|m| m.version()),
            Some(2)
        );
    }

    #[test]
    fn commit_does_nothing_if_nothing_changed_not_consistent_snapshot() {
        block_on(commit_does_nothing_if_nothing_changed(false))
    }

    #[test]
    fn commit_does_nothing_if_nothing_changed_consistent_snapshot() {
        block_on(commit_does_nothing_if_nothing_changed(true))
    }

    async fn commit_does_nothing_if_nothing_changed(consistent_snapshot: bool) {
        let mut repo = EphemeralRepository::<Pouf1>::new();
        let metadata1 = RepoBuilder::create(&mut repo)
            .trusted_root_keys(&[&KEYS[0]])
            .trusted_targets_keys(&[&KEYS[0]])
            .trusted_snapshot_keys(&[&KEYS[0]])
            .trusted_timestamp_keys(&[&KEYS[0]])
            .stage_root_with_builder(|builder| builder.consistent_snapshot(consistent_snapshot))
            .unwrap()
            .commit()
            .await
            .unwrap();

        let client_repo = EphemeralRepository::new();
        let mut client = Client::with_trusted_root(
            Config::default(),
            metadata1.root().unwrap(),
            client_repo,
            repo,
        )
        .await
        .unwrap();

        assert!(client.update().await.unwrap());
        assert_eq!(client.database().trusted_root().version(), 1);

        // Make sure doing another commit makes no changes.
        let mut parts = client.into_parts();
        let metadata2 = RepoBuilder::from_database(&mut parts.remote, &parts.database)
            .trusted_root_keys(&[&KEYS[0]])
            .trusted_targets_keys(&[&KEYS[0]])
            .trusted_snapshot_keys(&[&KEYS[0]])
            .trusted_timestamp_keys(&[&KEYS[0]])
            .commit()
            .await
            .unwrap();

        assert_metadata(&metadata2, None, None, None, None);

        let mut client = Client::from_parts(parts);
        assert!(!client.update().await.unwrap());
        assert_eq!(client.database().trusted_root().version(), 1);
    }

    #[test]
    fn root_chain_update_not_consistent() {
        block_on(check_root_chain_update(false));
    }

    #[test]
    fn root_chain_update_consistent() {
        block_on(check_root_chain_update(true));
    }

    async fn check_root_chain_update(consistent_snapshot: bool) {
        let mut repo = EphemeralRepository::<Pouf1>::new();

        // First, create the initial metadata. We initially sign the root
        // metadata with key 1.
        let metadata1 = RepoBuilder::create(&mut repo)
            .trusted_root_keys(&[&KEYS[1]])
            .trusted_targets_keys(&[&KEYS[0]])
            .trusted_snapshot_keys(&[&KEYS[0]])
            .trusted_timestamp_keys(&[&KEYS[0]])
            .stage_root_with_builder(|builder| builder.consistent_snapshot(consistent_snapshot))
            .unwrap()
            .commit()
            .await
            .unwrap();

        let client_repo = EphemeralRepository::new();
        let mut client = Client::with_trusted_root(
            Config::default(),
            metadata1.root().unwrap(),
            client_repo,
            repo,
        )
        .await
        .unwrap();

        assert!(client.update().await.unwrap());
        assert_eq!(client.database().trusted_root().version(), 1);
        assert_eq!(
            client
                .database()
                .trusted_root()
                .root_keys()
                .collect::<Vec<_>>(),
            vec![KEYS[1].public()],
        );

        // Another update should not fetch anything.
        assert!(!client.update().await.unwrap());
        assert_eq!(client.database().trusted_root().version(), 1);

        // Now bump the root to version 2. We sign the root metadata with both
        // key 1 and 2, but the builder should only trust key 2.
        let mut parts = client.into_parts();
        let _metadata2 = RepoBuilder::from_database(&mut parts.remote, &parts.database)
            .signing_root_keys(&[&KEYS[1]])
            .trusted_root_keys(&[&KEYS[2]])
            .trusted_targets_keys(&[&KEYS[0]])
            .trusted_snapshot_keys(&[&KEYS[0]])
            .trusted_timestamp_keys(&[&KEYS[0]])
            .commit()
            .await
            .unwrap();

        let mut client = Client::from_parts(parts);
        assert!(client.update().await.unwrap());
        assert_eq!(client.database().trusted_root().version(), 2);
        assert_eq!(
            client.database().trusted_root().consistent_snapshot(),
            consistent_snapshot
        );
        assert_eq!(
            client
                .database()
                .trusted_root()
                .root_keys()
                .collect::<Vec<_>>(),
            vec![KEYS[2].public()],
        );

        // Another update should not fetch anything.
        assert!(!client.update().await.unwrap());
        assert_eq!(client.database().trusted_root().version(), 2);

        // Now bump the root to version 3. The metadata will only be signed with
        // key 2, and trusted by key 2.
        let mut parts = client.into_parts();
        let _metadata3 = RepoBuilder::from_database(&mut parts.remote, &parts.database)
            .trusted_root_keys(&[&KEYS[2]])
            .trusted_targets_keys(&[&KEYS[0]])
            .trusted_snapshot_keys(&[&KEYS[0]])
            .trusted_timestamp_keys(&[&KEYS[0]])
            .stage_root()
            .unwrap()
            .commit()
            .await
            .unwrap();

        let mut client = Client::from_parts(parts);
        assert!(client.update().await.unwrap());
        assert_eq!(client.database().trusted_root().version(), 3);
        assert_eq!(
            client
                .database()
                .trusted_root()
                .root_keys()
                .collect::<Vec<_>>(),
            vec![KEYS[2].public()],
        );

        // Another update should not fetch anything.
        assert!(!client.update().await.unwrap());
        assert_eq!(client.database().trusted_root().version(), 3);
    }

    #[test]
    fn test_from_database_root_must_be_one_after_the_last() {
        block_on(async {
            let mut repo = EphemeralRepository::<Pouf1>::new();
            let metadata = RepoBuilder::create(&mut repo)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .commit()
                .await
                .unwrap();

            let db = Database::from_trusted_metadata(&metadata).unwrap();

            assert_matches!(
                RepoBuilder::from_database(&mut repo, &db)
                    .trusted_root_keys(&[&KEYS[0]])
                    .trusted_targets_keys(&[&KEYS[0]])
                    .trusted_snapshot_keys(&[&KEYS[0]])
                    .trusted_timestamp_keys(&[&KEYS[0]])
                    .stage_root_with_builder(|builder| builder.version(3))
                    .unwrap()
                    .commit()
                    .await,
                Err(Error::AttemptedMetadataRollBack {
                    role,
                    trusted_version: 1,
                    new_version: 3,
                })
                if role == MetadataPath::root()
            );
        })
    }

    #[test]
    fn test_add_target_not_consistent_snapshot() {
        block_on(async move {
            let mut repo = EphemeralRepository::<Pouf1>::new();

            let hash_algs = &[HashAlgorithm::Sha256, HashAlgorithm::Sha512];

            let target_path1 = TargetPath::new("foo/default").unwrap();
            let target_path1_hashed = TargetPath::new(
                "foo/522dd05a607a520657daa19c061a0271224030307117c2e661505e14601d1e44.default",
            )
            .unwrap();
            let target_file1: &[u8] = b"things fade, alternatives exclude";

            let target_path2 = TargetPath::new("foo/custom").unwrap();
            let target_path2_hashed = TargetPath::new(
                "foo/b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9.custom",
            )
            .unwrap();
            let target_file2: &[u8] = b"hello world";

            let metadata = RepoBuilder::create(&mut repo)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .add_target(target_path1.clone(), Cursor::new(target_file1))
                .await
                .unwrap()
                .target_hash_algorithms(hash_algs)
                .add_target(target_path2.clone(), Cursor::new(target_file2))
                .await
                .unwrap()
                .commit()
                .await
                .unwrap();

            // Make sure the targets were written correctly.
            let mut rdr = repo.fetch_target(&target_path1_hashed).await.unwrap();
            let mut buf = vec![];
            rdr.read_to_end(&mut buf).await.unwrap();
            drop(rdr);

            assert_eq!(&buf, target_file1);

            let mut rdr = repo.fetch_target(&target_path2_hashed).await.unwrap();
            let mut buf = vec![];
            rdr.read_to_end(&mut buf).await.unwrap();
            drop(rdr);

            assert_eq!(&buf, target_file2);

            let mut client = Client::with_trusted_root(
                Config::default(),
                metadata.root().unwrap(),
                EphemeralRepository::new(),
                repo,
            )
            .await
            .unwrap();

            client.update().await.unwrap();

            // Make sure the target descriptions are correct.
            assert_eq!(
                client
                    .fetch_target_description(&target_path1)
                    .await
                    .unwrap(),
                TargetDescription::from_slice(target_file1, &[HashAlgorithm::Sha256]).unwrap(),
            );

            assert_eq!(
                client
                    .fetch_target_description(&target_path2)
                    .await
                    .unwrap(),
                TargetDescription::from_slice(target_file2, hash_algs).unwrap(),
            );

            // Make sure we can fetch the targets.
            let mut rdr = client.fetch_target(&target_path1).await.unwrap();
            let mut buf = vec![];
            rdr.read_to_end(&mut buf).await.unwrap();
            assert_eq!(&buf, target_file1);
            drop(rdr);

            let mut rdr = client.fetch_target(&target_path2).await.unwrap();
            let mut buf = vec![];
            rdr.read_to_end(&mut buf).await.unwrap();
            assert_eq!(&buf, target_file2);
        })
    }

    #[test]
    fn test_add_target_consistent_snapshot() {
        block_on(async move {
            let mut repo = EphemeralRepository::<Pouf1>::new();

            let hash_algs = &[HashAlgorithm::Sha256, HashAlgorithm::Sha512];

            let target_path1 = TargetPath::new("foo/bar").unwrap();
            let target_file1: &[u8] = b"things fade, alternatives exclude";

            let target_path2 = TargetPath::new("baz").unwrap();
            let target_file2: &[u8] = b"hello world";
            let target_custom2 = hashmap! {
                "hello".into() => "world".into(),
            };

            let metadata = RepoBuilder::create(&mut repo)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .stage_root_with_builder(|builder| builder.consistent_snapshot(true))
                .unwrap()
                .target_hash_algorithms(hash_algs)
                .add_target(target_path1.clone(), Cursor::new(target_file1))
                .await
                .unwrap()
                .add_target_with_custom(
                    target_path2.clone(),
                    Cursor::new(target_file2),
                    target_custom2.clone(),
                )
                .await
                .unwrap()
                .commit()
                .await
                .unwrap();

            // Make sure the target was written correctly with hash prefixes.
            for (target_path, target_file) in &[
                (&target_path1, &target_file1),
                (&target_path2, &target_file2),
            ] {
                for hash_alg in hash_algs {
                    let hash = crypto::calculate_hash(target_file, hash_alg);
                    let target_path = target_path.with_hash_prefix(&hash).unwrap();

                    let mut rdr = repo.fetch_target(&target_path).await.unwrap();
                    let mut buf = vec![];
                    rdr.read_to_end(&mut buf).await.unwrap();

                    assert_eq!(&buf, *target_file);
                }
            }

            let mut client = Client::with_trusted_root(
                Config::default(),
                metadata.root().unwrap(),
                EphemeralRepository::new(),
                repo,
            )
            .await
            .unwrap();

            client.update().await.unwrap();

            // Make sure the target descriptions ar correct.
            assert_eq!(
                client
                    .fetch_target_description(&target_path1)
                    .await
                    .unwrap(),
                TargetDescription::from_slice(target_file1, hash_algs).unwrap(),
            );

            assert_eq!(
                client
                    .fetch_target_description(&target_path2)
                    .await
                    .unwrap(),
                TargetDescription::from_slice_with_custom(target_file2, hash_algs, target_custom2)
                    .unwrap(),
            );

            // Make sure we can fetch the targets.
            for (target_path, target_file) in &[
                (&target_path1, &target_file1),
                (&target_path2, &target_file2),
            ] {
                let mut rdr = client.fetch_target(target_path).await.unwrap();
                let mut buf = vec![];
                rdr.read_to_end(&mut buf).await.unwrap();
                assert_eq!(&buf, *target_file);
            }
        })
    }

    #[test]
    fn test_do_not_require_all_keys_to_be_online() {
        block_on(async {
            let mut remote = EphemeralRepository::<Pouf1>::new();

            // First, write some metadata to the repo.
            let expires1 = Utc.ymd(2038, 1, 1).and_hms(0, 0, 0);
            let metadata1 = RepoBuilder::create(&mut remote)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[1]])
                .trusted_snapshot_keys(&[&KEYS[2]])
                .trusted_timestamp_keys(&[&KEYS[3]])
                .stage_root_with_builder(|builder| {
                    builder.consistent_snapshot(true).expires(expires1)
                })
                .unwrap()
                .stage_targets_with_builder(|builder| builder.expires(expires1))
                .unwrap()
                .stage_snapshot_with_builder(|builder| builder.expires(expires1))
                .unwrap()
                .stage_timestamp_with_builder(|builder| builder.expires(expires1))
                .unwrap()
                .commit()
                .await
                .unwrap();

            // We wrote all the metadata.
            assert!(metadata1.root().is_some());
            assert!(metadata1.timestamp().is_some());
            assert!(metadata1.snapshot().is_some());
            assert!(metadata1.targets().is_some());

            let mut expected_metadata: BTreeMap<_, _> = vec![
                (
                    (MetadataPath::root(), MetadataVersion::Number(1)),
                    metadata1.root().unwrap().as_bytes(),
                ),
                (
                    (MetadataPath::root(), MetadataVersion::None),
                    metadata1.root().unwrap().as_bytes(),
                ),
                (
                    (MetadataPath::targets(), MetadataVersion::Number(1)),
                    metadata1.targets().unwrap().as_bytes(),
                ),
                (
                    (MetadataPath::targets(), MetadataVersion::None),
                    metadata1.targets().unwrap().as_bytes(),
                ),
                (
                    (MetadataPath::snapshot(), MetadataVersion::Number(1)),
                    metadata1.snapshot().unwrap().as_bytes(),
                ),
                (
                    (MetadataPath::snapshot(), MetadataVersion::None),
                    metadata1.snapshot().unwrap().as_bytes(),
                ),
                (
                    (MetadataPath::timestamp(), MetadataVersion::None),
                    metadata1.timestamp().unwrap().as_bytes(),
                ),
            ]
            .into_iter()
            .collect();

            assert_repo(&remote, &expected_metadata);

            let mut db = Database::from_trusted_metadata(&metadata1).unwrap();

            // Next, write another batch, but only have the timestamp, snapshot, and targets keys.
            let expires2 = Utc.ymd(2038, 1, 2).and_hms(0, 0, 0);
            let metadata2 = RepoBuilder::from_database(&mut remote, &db)
                .trusted_targets_keys(&[&KEYS[1]])
                .trusted_snapshot_keys(&[&KEYS[2]])
                .trusted_timestamp_keys(&[&KEYS[3]])
                .skip_root()
                .stage_targets_with_builder(|builder| builder.expires(expires2))
                .unwrap()
                .stage_snapshot_with_builder(|builder| builder.expires(expires2))
                .unwrap()
                .stage_timestamp_with_builder(|builder| builder.expires(expires2))
                .unwrap()
                .commit()
                .await
                .unwrap();

            assert!(db.update_metadata(&metadata2).unwrap());

            assert!(metadata2.root().is_none());
            assert!(metadata2.targets().is_some());
            assert!(metadata2.snapshot().is_some());
            assert!(metadata2.timestamp().is_some());

            expected_metadata.extend(
                vec![
                    (
                        (MetadataPath::targets(), MetadataVersion::Number(2)),
                        metadata2.targets().unwrap().as_bytes(),
                    ),
                    (
                        (MetadataPath::targets(), MetadataVersion::None),
                        metadata2.targets().unwrap().as_bytes(),
                    ),
                    (
                        (MetadataPath::snapshot(), MetadataVersion::Number(2)),
                        metadata2.snapshot().unwrap().as_bytes(),
                    ),
                    (
                        (MetadataPath::snapshot(), MetadataVersion::None),
                        metadata2.snapshot().unwrap().as_bytes(),
                    ),
                    (
                        (MetadataPath::timestamp(), MetadataVersion::None),
                        metadata2.timestamp().unwrap().as_bytes(),
                    ),
                ]
                .into_iter(),
            );

            assert_repo(&remote, &expected_metadata);

            // Now, only have the timestamp and snapshot keys online.
            let expires3 = Utc.ymd(2038, 1, 3).and_hms(0, 0, 0);
            let metadata3 = RepoBuilder::from_database(&mut remote, &db)
                .trusted_snapshot_keys(&[&KEYS[2]])
                .trusted_timestamp_keys(&[&KEYS[3]])
                .skip_root()
                .skip_targets()
                .stage_snapshot_with_builder(|builder| builder.expires(expires3))
                .unwrap()
                .stage_timestamp_with_builder(|builder| builder.expires(expires3))
                .unwrap()
                .commit()
                .await
                .unwrap();

            assert!(db.update_metadata(&metadata3).unwrap());

            // We only have timestamp and snapshot.
            assert!(metadata3.root().is_none());
            assert!(metadata3.targets().is_none());
            assert!(metadata3.snapshot().is_some());
            assert!(metadata3.timestamp().is_some());

            expected_metadata.extend(
                vec![
                    (
                        (MetadataPath::snapshot(), MetadataVersion::Number(3)),
                        metadata3.snapshot().unwrap().as_bytes(),
                    ),
                    (
                        (MetadataPath::snapshot(), MetadataVersion::None),
                        metadata3.snapshot().unwrap().as_bytes(),
                    ),
                    (
                        (MetadataPath::timestamp(), MetadataVersion::None),
                        metadata3.timestamp().unwrap().as_bytes(),
                    ),
                ]
                .into_iter(),
            );

            assert_repo(&remote, &expected_metadata);

            // Finally, only have the timestamp keys online.
            let expires4 = Utc.ymd(2038, 1, 4).and_hms(0, 0, 0);
            let metadata4 = RepoBuilder::from_database(&mut remote, &db)
                .trusted_timestamp_keys(&[&KEYS[3]])
                .skip_root()
                .skip_targets()
                .skip_snapshot()
                .stage_timestamp_with_builder(|builder| builder.expires(expires4))
                .unwrap()
                .commit()
                .await
                .unwrap();

            assert!(db.update_metadata(&metadata4).unwrap());

            // We only have timestamp and snapshot.
            assert!(metadata4.root().is_none());
            assert!(metadata4.targets().is_none());
            assert!(metadata4.snapshot().is_none());
            assert!(metadata4.timestamp().is_some());

            expected_metadata.extend(
                vec![(
                    (MetadataPath::timestamp(), MetadataVersion::None),
                    metadata4.timestamp().unwrap().as_bytes(),
                )]
                .into_iter(),
            );

            assert_repo(&remote, &expected_metadata);
        })
    }

    #[test]
    fn test_builder_inherits_from_trusted_targets() {
        block_on(async move {
            let mut repo = EphemeralRepository::<Pouf1>::new();

            let expires = Utc.ymd(2038, 1, 4).and_hms(0, 0, 0);
            let hash_algs = &[HashAlgorithm::Sha256, HashAlgorithm::Sha512];
            let delegation_key = &KEYS[0];
            let delegation_path = MetadataPath::new("delegations").unwrap();

            let target_path1 = TargetPath::new("target1").unwrap();
            let target_file1: &[u8] = b"target1 file";

            let delegated_target_path1 = TargetPath::new("delegations/delegation1").unwrap();
            let delegated_target_file1: &[u8] = b"delegation1 file";

            let delegation1 = Delegation::builder(delegation_path.clone())
                .key(delegation_key.public())
                .delegate_path(TargetPath::new("delegations/").unwrap())
                .build()
                .unwrap();

            let delegated_targets1 = TargetsMetadataBuilder::new()
                .insert_target_from_slice(
                    delegated_target_path1,
                    delegated_target_file1,
                    &[HashAlgorithm::Sha256],
                )
                .unwrap()
                .signed::<Pouf1>(delegation_key)
                .unwrap();
            let raw_delegated_targets = delegated_targets1.to_raw().unwrap();

            let metadata1 = RepoBuilder::create(&mut repo)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .stage_root()
                .unwrap()
                .target_hash_algorithms(hash_algs)
                .add_target(target_path1.clone(), Cursor::new(target_file1))
                .await
                .unwrap()
                .add_delegation_key(delegation_key.public().clone())
                .add_delegation_role(delegation1.clone())
                .stage_targets()
                .unwrap()
                .stage_snapshot_with_builder(|builder| {
                    builder.insert_metadata_description(
                        delegation_path.clone(),
                        MetadataDescription::from_slice(
                            raw_delegated_targets.as_bytes(),
                            1,
                            &[HashAlgorithm::Sha256],
                        )
                        .unwrap(),
                    )
                })
                .unwrap()
                .commit()
                .await
                .unwrap();

            // Next, create a new commit where we add a new target and delegation. This should copy
            // over the old targets and delegations.
            let mut database = Database::from_trusted_metadata(&metadata1).unwrap();

            let target_path2 = TargetPath::new("bar").unwrap();
            let target_file2: &[u8] = b"bar file";

            let delegated_target_path2 = TargetPath::new("delegations/delegation2").unwrap();
            let delegated_target_file2: &[u8] = b"delegation2 file";

            let delegation2 = Delegation::builder(delegation_path.clone())
                .key(delegation_key.public())
                .delegate_path(TargetPath::new("delegations/").unwrap())
                .build()
                .unwrap();

            let delegated_targets2 = TargetsMetadataBuilder::new()
                .insert_target_from_slice(
                    delegated_target_path2,
                    delegated_target_file2,
                    &[HashAlgorithm::Sha256],
                )
                .unwrap()
                .signed::<Pouf1>(delegation_key)
                .unwrap();
            let raw_delegated_targets = delegated_targets2.to_raw().unwrap();

            let metadata2 = RepoBuilder::from_database(&mut repo, &database)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .stage_root()
                .unwrap()
                .target_hash_algorithms(hash_algs)
                .add_target(target_path2.clone(), Cursor::new(target_file2))
                .await
                .unwrap()
                .add_delegation_role(delegation2.clone())
                .stage_targets_with_builder(|b| b.expires(expires))
                .unwrap()
                .stage_snapshot_with_builder(|builder| {
                    builder.insert_metadata_description(
                        delegation_path.clone(),
                        MetadataDescription::from_slice(
                            raw_delegated_targets.as_bytes(),
                            1,
                            &[HashAlgorithm::Sha256],
                        )
                        .unwrap(),
                    )
                })
                .unwrap()
                .commit()
                .await
                .unwrap();

            database.update_metadata(&metadata2).unwrap();

            assert_eq!(
                &**database.trusted_targets().unwrap(),
                &TargetsMetadataBuilder::new()
                    .version(2)
                    .expires(expires)
                    .insert_target_from_slice(target_path1.clone(), target_file1, hash_algs)
                    .unwrap()
                    .insert_target_from_slice(target_path2.clone(), target_file2, hash_algs)
                    .unwrap()
                    .delegations(
                        DelegationsBuilder::new()
                            .key(delegation_key.public().clone())
                            .role(delegation1)
                            .role(delegation2)
                            .build()
                            .unwrap()
                    )
                    .build()
                    .unwrap()
            )
        })
    }

    #[test]
    fn test_builder_rotating_keys_refreshes_metadata() {
        block_on(async move {
            let mut repo = EphemeralRepository::<Pouf1>::new();

            let metadata1 = RepoBuilder::create(&mut repo)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .commit()
                .await
                .unwrap();

            let mut db = Database::from_trusted_metadata(&metadata1).unwrap();

            // Because of [update-root], rotating any root keys should make a new timestamp and
            // snapshot.
            //
            // FIXME(#297): This also purges targets, even though that's not conforming to the spec.
            //
            // [update-root]: https://theupdateframework.github.io/specification/v1.0.30/#update-root
            let metadata2 = RepoBuilder::from_database(&mut repo, &db)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[1]])
                .commit()
                .await
                .unwrap();

            assert!(metadata2.root().is_some());
            assert!(metadata2.targets().is_some());
            assert!(metadata2.snapshot().is_some());
            assert!(metadata2.timestamp().is_some());

            db.update_metadata(&metadata2).unwrap();

            assert_eq!(db.trusted_root().version(), 2);
            assert_eq!(db.trusted_targets().unwrap().version(), 2);
            assert_eq!(db.trusted_snapshot().unwrap().version(), 2);
            assert_eq!(db.trusted_timestamp().unwrap().version(), 2);

            // Note that rotating the timestamp keys purges all the metadata, so add it back in.

            // Rotating the snapshot key should make new metadata.
            let metadata3 = RepoBuilder::from_database(&mut repo, &db)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[1]])
                .trusted_timestamp_keys(&[&KEYS[1]])
                .commit()
                .await
                .unwrap();

            assert!(metadata3.root().is_some());
            assert!(metadata2.targets().is_some());
            assert!(metadata2.snapshot().is_some());
            assert!(metadata2.timestamp().is_some());

            db.update_metadata(&metadata3).unwrap();

            assert_eq!(db.trusted_root().version(), 3);
            assert_eq!(db.trusted_targets().unwrap().version(), 3);
            assert_eq!(db.trusted_snapshot().unwrap().version(), 3);
            assert_eq!(db.trusted_timestamp().unwrap().version(), 3);

            // Rotating the targets key should make a new targets, snapshot, and timestamp.
            let metadata4 = RepoBuilder::from_database(&mut repo, &db)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[1]])
                .trusted_snapshot_keys(&[&KEYS[1]])
                .trusted_timestamp_keys(&[&KEYS[1]])
                .commit()
                .await
                .unwrap();

            assert!(metadata4.root().is_some());
            assert!(metadata4.targets().is_some());
            assert!(metadata4.snapshot().is_some());
            assert!(metadata4.timestamp().is_some());

            db.update_metadata(&metadata4).unwrap();

            assert_eq!(db.trusted_root().version(), 4);
            assert_eq!(db.trusted_targets().unwrap().version(), 4);
            assert_eq!(db.trusted_snapshot().unwrap().version(), 4);
            assert_eq!(db.trusted_timestamp().unwrap().version(), 4);

            // Rotating the root key should make a new targets, snapshot, and timestamp.
            let metadata5 = RepoBuilder::from_database(&mut repo, &db)
                .signing_root_keys(&[&KEYS[0]])
                .trusted_root_keys(&[&KEYS[1]])
                .trusted_targets_keys(&[&KEYS[1]])
                .trusted_snapshot_keys(&[&KEYS[1]])
                .trusted_timestamp_keys(&[&KEYS[1]])
                .commit()
                .await
                .unwrap();

            assert!(metadata5.root().is_some());
            assert!(metadata5.targets().is_some());
            assert!(metadata5.snapshot().is_some());
            assert!(metadata5.timestamp().is_some());

            db.update_metadata(&metadata5).unwrap();

            assert_eq!(db.trusted_root().version(), 5);
            assert_eq!(db.trusted_targets().unwrap().version(), 5);
            assert_eq!(db.trusted_snapshot().unwrap().version(), 5);
            assert_eq!(db.trusted_timestamp().unwrap().version(), 5);
        })
    }

    #[test]
    fn test_builder_expired_metadata_refreshes_metadata() {
        block_on(async move {
            let mut repo = EphemeralRepository::<Pouf1>::new();

            let epoch = Utc.timestamp(0, 0);
            let root_expires = Duration::seconds(40);
            let targets_expires = Duration::seconds(30);
            let snapshot_expires = Duration::seconds(20);
            let timestamp_expires = Duration::seconds(10);

            let current_time = epoch;
            let metadata1 = RepoBuilder::create(&mut repo)
                .current_time(current_time)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .root_expiration_duration(root_expires)
                .targets_expiration_duration(targets_expires)
                .snapshot_expiration_duration(snapshot_expires)
                .timestamp_expiration_duration(timestamp_expires)
                .commit()
                .await
                .unwrap();

            let mut db =
                Database::from_trusted_metadata_with_start_time(&metadata1, &current_time).unwrap();

            // Advance time to past the timestamp expiration.
            let current_time = epoch + timestamp_expires + Duration::seconds(1);
            let metadata2 = RepoBuilder::from_database(&mut repo, &db)
                .current_time(current_time)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .commit()
                .await
                .unwrap();

            assert!(metadata2.root().is_none());
            assert!(metadata2.targets().is_none());
            assert!(metadata2.snapshot().is_none());
            assert!(metadata2.timestamp().is_some());

            db.update_metadata_with_start_time(&metadata2, &current_time)
                .unwrap();

            assert_eq!(db.trusted_root().version(), 1);
            assert_eq!(db.trusted_targets().unwrap().version(), 1);
            assert_eq!(db.trusted_snapshot().unwrap().version(), 1);
            assert_eq!(db.trusted_timestamp().unwrap().version(), 2);

            // Advance time to past the snapshot expiration.
            let current_time = epoch + snapshot_expires + Duration::seconds(1);
            let metadata3 = RepoBuilder::from_database(&mut repo, &db)
                .current_time(current_time)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .commit()
                .await
                .unwrap();

            assert!(metadata3.root().is_none());
            assert!(metadata3.targets().is_none());
            assert!(metadata3.snapshot().is_some());
            assert!(metadata3.timestamp().is_some());

            db.update_metadata_with_start_time(&metadata3, &current_time)
                .unwrap();

            assert_eq!(db.trusted_root().version(), 1);
            assert_eq!(db.trusted_targets().unwrap().version(), 1);
            assert_eq!(db.trusted_snapshot().unwrap().version(), 2);
            assert_eq!(db.trusted_timestamp().unwrap().version(), 3);

            // Advance time to past the targets expiration.
            let current_time = epoch + targets_expires + Duration::seconds(1);
            let metadata4 = RepoBuilder::from_database(&mut repo, &db)
                .current_time(current_time)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .commit()
                .await
                .unwrap();

            assert!(metadata4.root().is_none());
            assert!(metadata4.targets().is_some());
            assert!(metadata4.snapshot().is_some());
            assert!(metadata4.timestamp().is_some());

            db.update_metadata_with_start_time(&metadata4, &current_time)
                .unwrap();

            assert_eq!(db.trusted_root().version(), 1);
            assert_eq!(db.trusted_targets().unwrap().version(), 2);
            assert_eq!(db.trusted_snapshot().unwrap().version(), 3);
            assert_eq!(db.trusted_timestamp().unwrap().version(), 4);

            // Advance time to past the root expiration.
            //
            // Because of [update-root], rotating any root keys should make a new timestamp and
            // snapshot.
            //
            // [update-root]: https://theupdateframework.github.io/specification/v1.0.30/#update-root
            let current_time = epoch + root_expires + Duration::seconds(1);
            let metadata5 = RepoBuilder::from_database(&mut repo, &db)
                .current_time(current_time)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .commit()
                .await
                .unwrap();

            assert!(metadata5.root().is_some());
            assert!(metadata5.targets().is_some());
            assert!(metadata5.snapshot().is_some());
            assert!(metadata5.timestamp().is_some());

            db.update_metadata_with_start_time(&metadata5, &current_time)
                .unwrap();

            assert_eq!(db.trusted_root().version(), 2);
            assert_eq!(db.trusted_targets().unwrap().version(), 3);
            assert_eq!(db.trusted_snapshot().unwrap().version(), 4);
            assert_eq!(db.trusted_timestamp().unwrap().version(), 5);
        })
    }

    #[test]
    fn test_adding_target_refreshes_metadata() {
        block_on(async move {
            let mut repo = EphemeralRepository::<Pouf1>::new();

            let metadata1 = RepoBuilder::create(&mut repo)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .commit()
                .await
                .unwrap();

            let mut db = Database::from_trusted_metadata(&metadata1).unwrap();

            let target_path = TargetPath::new("foo").unwrap();
            let target_file: &[u8] = b"foo file";

            let metadata2 = RepoBuilder::from_database(&mut repo, &db)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .add_target(target_path, Cursor::new(target_file))
                .await
                .unwrap()
                .commit()
                .await
                .unwrap();

            assert!(metadata2.root().is_none());
            assert!(metadata2.targets().is_some());
            assert!(metadata2.snapshot().is_some());
            assert!(metadata2.timestamp().is_some());

            db.update_metadata(&metadata2).unwrap();

            assert_eq!(db.trusted_root().version(), 1);
            assert_eq!(db.trusted_targets().unwrap().version(), 2);
            assert_eq!(db.trusted_snapshot().unwrap().version(), 2);
            assert_eq!(db.trusted_timestamp().unwrap().version(), 2);
        })
    }

    #[test]
    fn test_time_versioning() {
        block_on(async move {
            let mut repo = EphemeralRepository::<Pouf1>::new();

            let current_time = Utc.timestamp(5, 0);
            let metadata = RepoBuilder::create(&mut repo)
                .current_time(current_time)
                .time_versioning(true)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .commit()
                .await
                .unwrap();

            let mut db =
                Database::from_trusted_metadata_with_start_time(&metadata, &current_time).unwrap();

            // The initial version should be the current time.
            assert_eq!(db.trusted_root().version(), 1);
            assert_eq!(db.trusted_targets().map(|m| m.version()), Some(5));
            assert_eq!(db.trusted_snapshot().map(|m| m.version()), Some(5));
            assert_eq!(db.trusted_timestamp().map(|m| m.version()), Some(5));

            // Generating metadata for the same timestamp should advance it by 1.
            let metadata = RepoBuilder::from_database(&mut repo, &db)
                .current_time(current_time)
                .time_versioning(true)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .stage_root()
                .unwrap()
                .stage_targets()
                .unwrap()
                .commit()
                .await
                .unwrap();

            db.update_metadata_with_start_time(&metadata, &current_time)
                .unwrap();

            assert_eq!(db.trusted_root().version(), 2);
            assert_eq!(db.trusted_targets().map(|m| m.version()), Some(6));
            assert_eq!(db.trusted_snapshot().map(|m| m.version()), Some(6));
            assert_eq!(db.trusted_timestamp().map(|m| m.version()), Some(6));

            // Generating metadata for a new timestamp should advance the versions to that amount.
            let current_time = Utc.timestamp(10, 0);
            let metadata = RepoBuilder::from_database(&mut repo, &db)
                .current_time(current_time)
                .time_versioning(true)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .stage_root()
                .unwrap()
                .stage_targets()
                .unwrap()
                .commit()
                .await
                .unwrap();

            db.update_metadata_with_start_time(&metadata, &current_time)
                .unwrap();

            assert_eq!(db.trusted_root().version(), 3);
            assert_eq!(db.trusted_targets().map(|m| m.version()), Some(10));
            assert_eq!(db.trusted_snapshot().map(|m| m.version()), Some(10));
            assert_eq!(db.trusted_timestamp().map(|m| m.version()), Some(10));
        })
    }

    #[test]
    fn test_time_versioning_falls_back_to_monotonic() {
        block_on(async move {
            let mut repo = EphemeralRepository::<Pouf1>::new();

            // zero timestamp should initialize to 1.
            let current_time = Utc.timestamp(0, 0);
            let metadata = RepoBuilder::create(&mut repo)
                .current_time(current_time)
                .time_versioning(true)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .commit()
                .await
                .unwrap();

            let mut db =
                Database::from_trusted_metadata_with_start_time(&metadata, &current_time).unwrap();

            assert_eq!(db.trusted_root().version(), 1);
            assert_eq!(db.trusted_targets().map(|m| m.version()), Some(1));
            assert_eq!(db.trusted_snapshot().map(|m| m.version()), Some(1));
            assert_eq!(db.trusted_timestamp().map(|m| m.version()), Some(1));

            // A sub-second timestamp should advance the version by 1.
            let current_time = Utc.timestamp(0, 3);
            let metadata = RepoBuilder::from_database(&mut repo, &db)
                .current_time(current_time)
                .time_versioning(true)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .stage_root()
                .unwrap()
                .stage_targets()
                .unwrap()
                .commit()
                .await
                .unwrap();

            db.update_metadata_with_start_time(&metadata, &current_time)
                .unwrap();

            assert_eq!(db.trusted_root().version(), 2);
            assert_eq!(db.trusted_targets().map(|m| m.version()), Some(2));
            assert_eq!(db.trusted_snapshot().map(|m| m.version()), Some(2));
            assert_eq!(db.trusted_timestamp().map(|m| m.version()), Some(2));
        })
    }

    #[test]
    fn test_builder_errs_if_no_keys() {
        block_on(async move {
            let repo = EphemeralRepository::<Pouf1>::new();

            let metadata = RepoBuilder::create(&repo)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .commit()
                .await
                .unwrap();

            let db = Database::from_trusted_metadata(&metadata).unwrap();

            match RepoBuilder::from_database(&repo, &db).stage_root() {
                Err(Error::MetadataRoleDoesNotHaveEnoughKeyIds {
                    role,
                    key_ids: 0,
                    threshold: 1,
                }) if role == MetadataPath::root() => {}
                Err(err) => panic!("unexpected error: {}", err),
                Ok(_) => panic!("unexpected success"),
            }

            match RepoBuilder::from_database(&repo, &db)
                .trusted_root_keys(&[&KEYS[0]])
                .stage_root_if_necessary()
                .unwrap()
                .stage_targets()
            {
                Err(Error::MissingPrivateKey { role }) if role == MetadataPath::targets() => {}
                Err(err) => panic!("unexpected error: {}", err),
                Ok(_) => panic!("unexpected success"),
            }

            match RepoBuilder::from_database(&repo, &db)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .stage_root_if_necessary()
                .unwrap()
                .stage_targets_if_necessary()
                .unwrap()
                .stage_snapshot()
            {
                Err(Error::MissingPrivateKey { role }) if role == MetadataPath::snapshot() => {}
                Err(err) => panic!("unexpected error: {}", err),
                Ok(_) => panic!("unexpected success"),
            }

            match RepoBuilder::from_database(&repo, &db)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .stage_root_if_necessary()
                .unwrap()
                .stage_targets_if_necessary()
                .unwrap()
                .stage_snapshot_if_necessary()
                .unwrap()
                .stage_timestamp()
            {
                Err(Error::MissingPrivateKey { role }) if role == MetadataPath::timestamp() => {}
                Err(err) => panic!("unexpected error: {}", err),
                Ok(_) => panic!("unexpected success"),
            }
        })
    }
}
