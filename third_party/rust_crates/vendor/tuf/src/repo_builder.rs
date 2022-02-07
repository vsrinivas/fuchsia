//! Repository Builder

use {
    crate::{
        crypto::{self, HashAlgorithm, PrivateKey},
        database::Database,
        interchange::DataInterchange,
        metadata::{
            Metadata, MetadataDescription, MetadataPath, MetadataVersion, RawSignedMetadata,
            RawSignedMetadataSet, RawSignedMetadataSetBuilder, Role, RootMetadata,
            RootMetadataBuilder, SignedMetadataBuilder, SnapshotMetadata, SnapshotMetadataBuilder,
            TargetDescription, TargetPath, TargetsMetadata, TargetsMetadataBuilder,
            TimestampMetadata, TimestampMetadataBuilder,
        },
        repository::RepositoryStorage,
        Error, Result,
    },
    chrono::{Duration, Utc},
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
    impl<D: DataInterchange> Sealed for Targets<D> {}
    impl<D: DataInterchange> Sealed for Snapshot<D> {}
    impl<D: DataInterchange> Sealed for Timestamp<D> {}
    impl<D: DataInterchange> Sealed for Done<D> {}
}

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
pub struct Targets<D: DataInterchange> {
    staged_root: Option<Staged<D, RootMetadata>>,
    builder: TargetsMetadataBuilder,
    file_hash_algorithms: Vec<HashAlgorithm>,
}

impl<D: DataInterchange> Targets<D> {
    fn new(staged_root: Option<Staged<D, RootMetadata>>) -> Self {
        Self {
            staged_root,
            builder: TargetsMetadataBuilder::new().expires(Utc::now() + Duration::days(90)),
            file_hash_algorithms: vec![HashAlgorithm::Sha256],
        }
    }
}

impl<D: DataInterchange> State for Targets<D> {}

/// State to stage a snapshot metadata.
#[doc(hidden)]
pub struct Snapshot<D: DataInterchange> {
    staged_root: Option<Staged<D, RootMetadata>>,
    staged_targets: Option<Staged<D, TargetsMetadata>>,
    include_targets_length: bool,
    targets_hash_algorithms: Vec<HashAlgorithm>,
    inherit_targets: bool,
}

impl<D: DataInterchange> State for Snapshot<D> {}

impl<D: DataInterchange> Snapshot<D> {
    fn new(
        staged_root: Option<Staged<D, RootMetadata>>,
        staged_targets: Option<Staged<D, TargetsMetadata>>,
    ) -> Self {
        Self {
            staged_root,
            staged_targets,
            include_targets_length: false,
            targets_hash_algorithms: vec![],
            inherit_targets: true,
        }
    }

    fn targets_description(&self) -> Result<Option<MetadataDescription>> {
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
pub struct Timestamp<D: DataInterchange> {
    staged_root: Option<Staged<D, RootMetadata>>,
    staged_targets: Option<Staged<D, TargetsMetadata>>,
    staged_snapshot: Option<Staged<D, SnapshotMetadata>>,
    include_snapshot_length: bool,
    snapshot_hash_algorithms: Vec<HashAlgorithm>,
}

impl<D: DataInterchange> Timestamp<D> {
    fn new(state: Snapshot<D>, staged_snapshot: Option<Staged<D, SnapshotMetadata>>) -> Self {
        Self {
            staged_root: state.staged_root,
            staged_targets: state.staged_targets,
            staged_snapshot,
            include_snapshot_length: false,
            snapshot_hash_algorithms: vec![],
        }
    }

    fn snapshot_description(&self) -> Result<Option<MetadataDescription>> {
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

impl<D: DataInterchange> State for Timestamp<D> {}

/// The final state for building repository metadata.
pub struct Done<D: DataInterchange> {
    staged_root: Option<Staged<D, RootMetadata>>,
    staged_targets: Option<Staged<D, TargetsMetadata>>,
    staged_snapshot: Option<Staged<D, SnapshotMetadata>>,
    staged_timestamp: Option<Staged<D, TimestampMetadata>>,
}

impl<D: DataInterchange> State for Done<D> {}

struct Staged<D: DataInterchange, M: Metadata> {
    metadata: M,
    raw: RawSignedMetadata<D, M>,
}

struct RepoContext<'a, D, R>
where
    D: DataInterchange + Sync,
    R: RepositoryStorage<D>,
{
    repo: R,
    db: Option<&'a Database<D>>,
    signing_root_keys: Vec<&'a dyn PrivateKey>,
    signing_targets_keys: Vec<&'a dyn PrivateKey>,
    signing_snapshot_keys: Vec<&'a dyn PrivateKey>,
    signing_timestamp_keys: Vec<&'a dyn PrivateKey>,
    trusted_root_keys: Vec<&'a dyn PrivateKey>,
    trusted_targets_keys: Vec<&'a dyn PrivateKey>,
    trusted_snapshot_keys: Vec<&'a dyn PrivateKey>,
    trusted_timestamp_keys: Vec<&'a dyn PrivateKey>,
    _interchange: PhantomData<D>,
}

fn sign<'a, D, I, M>(meta: &M, keys: I) -> Result<RawSignedMetadata<D, M>>
where
    D: DataInterchange,
    M: Metadata,
    I: IntoIterator<Item = &'a &'a dyn PrivateKey>,
{
    // Sign the root.
    let mut signed_builder = SignedMetadataBuilder::<D, _>::from_metadata(meta)?;
    for key in keys {
        signed_builder = signed_builder.sign(*key)?;
    }

    signed_builder.build().to_raw()
}

/// This helper builder simplifies the process of creating new metadata.
pub struct RepoBuilder<'a, D, R, S = Root>
where
    D: DataInterchange + Sync,
    R: RepositoryStorage<D>,
    S: State,
{
    ctx: RepoContext<'a, D, R>,
    state: S,
}

impl<'a, D, R> RepoBuilder<'a, D, R, Root>
where
    D: DataInterchange + Sync,
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
    /// #         interchange::Json,
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
    /// let mut repo = EphemeralRepository::<Json>::new();
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
                signing_root_keys: vec![],
                signing_targets_keys: vec![],
                signing_snapshot_keys: vec![],
                signing_timestamp_keys: vec![],
                trusted_root_keys: vec![],
                trusted_targets_keys: vec![],
                trusted_snapshot_keys: vec![],
                trusted_timestamp_keys: vec![],
                _interchange: PhantomData,
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
    /// #         interchange::Json,
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
    ///  let mut repo = EphemeralRepository::<Json>::new();
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
                signing_root_keys: vec![],
                signing_targets_keys: vec![],
                signing_snapshot_keys: vec![],
                signing_timestamp_keys: vec![],
                trusted_root_keys: vec![],
                trusted_targets_keys: vec![],
                trusted_snapshot_keys: vec![],
                trusted_timestamp_keys: vec![],
                _interchange: PhantomData,
            },
            state: Root { builder },
        }
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
                Error::VerificationFailure("root version should be less than max u32".into())
            })?
        } else {
            1
        };

        let root_builder = self
            .state
            .builder
            .version(next_version)
            .expires(Utc::now() + Duration::days(365));
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
        // If we don't have a database yet, we need to stage the first root
        // metadata.
        let root = if let Some(db) = self.ctx.db {
            db.trusted_root()
        } else {
            return true;
        };

        if root.expires() <= &Utc::now() {
            return true;
        }

        // Sign the metadata if we passed in any old root keys.
        if !self.ctx.signing_root_keys.is_empty() {
            return true;
        }

        // Otherwise, see if any of the keys have changed.
        let root_keys_count = root.root_keys().count();
        if root_keys_count != self.ctx.trusted_root_keys.len() {
            return true;
        }

        for &key in &self.ctx.trusted_root_keys {
            if !root.root_keys().any(|x| x == key.public()) {
                return true;
            }
        }

        for &key in &self.ctx.trusted_targets_keys {
            if !root.targets_keys().any(|x| x == key.public()) {
                return true;
            }
        }

        for &key in &self.ctx.trusted_snapshot_keys {
            if !root.snapshot_keys().any(|x| x == key.public()) {
                return true;
            }
        }

        for &key in &self.ctx.trusted_timestamp_keys {
            if !root.timestamp_keys().any(|x| x == key.public()) {
                return true;
            }
        }

        false
    }
}

impl<'a, D, R> RepoBuilder<'a, D, R, Targets<D>>
where
    D: DataInterchange + Sync,
    R: RepositoryStorage<D>,
{
    /// Whether or not to include the length of the targets, and any delegated targets, in the
    /// new snapshot.
    pub fn file_hash_algorithms(mut self, algorithms: &[HashAlgorithm]) -> Self {
        self.state.file_hash_algorithms = algorithms.to_vec();
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
    /// This will hash the file with the hash specified in [RepoBuilder::file_hash_algorithms]. If
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
    /// This will hash the file with the hash specified in [RepoBuilder::file_hash_algorithms]. If
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
            return Err(Error::MissingMetadata(Role::Root));
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

        self.state.builder = self
            .state
            .builder
            .insert_target_description(target_path, target_description);

        Ok(self)
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
        let next_version = if let Some(db) = self.ctx.db {
            if let Some(trusted_targets) = db.trusted_targets() {
                trusted_targets.version().checked_add(1).ok_or_else(|| {
                    Error::VerificationFailure("targets version should be less than max u32".into())
                })?
            } else {
                1
            }
        } else {
            1
        };

        let targets_builder = self.state.builder.version(next_version);
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

    /// Commit the metadata for this repository without validating it.
    ///
    /// Warning: This can write invalid metadata to a repository without
    /// validating that it is correct.
    #[cfg(test)]
    pub async fn commit_skip_validation(self) -> Result<RawSignedMetadataSet<D>> {
        self.stage_targets_if_necessary()?
            .commit_skip_validation()
            .await
    }

    fn need_new_targets(&self) -> bool {
        let db = if let Some(ref db) = self.ctx.db {
            db
        } else {
            return true;
        };

        if let Some(targets) = db.trusted_targets() {
            if targets.expires() <= &Utc::now() {
                return true;
            }
        } else {
            return true;
        }

        false
    }
}

impl<'a, D, R> RepoBuilder<'a, D, R, Snapshot<D>>
where
    D: DataInterchange + Sync,
    R: RepositoryStorage<D>,
{
    /// Whether or not to include the length of the targets, and any delegated targets, in the
    /// new snapshot.
    pub fn snapshot_includes_length(mut self, include_targets_lengths: bool) -> Self {
        self.state.include_targets_length = include_targets_lengths;
        self
    }

    /// Whether or not to include the hashes of the targets, and any delegated targets, in the
    /// new snapshot.
    pub fn snapshot_includes_hashes(mut self, hashes: &[HashAlgorithm]) -> Self {
        self.state.targets_hash_algorithms = hashes.to_vec();
        self
    }

    /// Whether or not to inherit targets, and delegated targets, from the trusted snapshot in the
    /// new snapshot.
    pub fn inherit_targets(mut self, inherit_targets: bool) -> Self {
        self.state.inherit_targets = inherit_targets;
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
        let next_version = if let Some(db) = self.ctx.db {
            if let Some(trusted_snapshot) = db.trusted_snapshot() {
                trusted_snapshot.version().checked_add(1).ok_or_else(|| {
                    Error::VerificationFailure(
                        "snapshot version should be less than max u32".into(),
                    )
                })?
            } else {
                1
            }
        } else {
            1
        };

        let mut snapshot_builder = SnapshotMetadataBuilder::new()
            .version(next_version)
            .expires(Utc::now() + Duration::days(7));

        // Insert all the metadata from the trusted snapshot.
        if self.state.inherit_targets {
            if let Some(db) = self.ctx.db {
                if let Some(snapshot) = db.trusted_snapshot() {
                    for (path, description) in snapshot.meta() {
                        snapshot_builder = snapshot_builder
                            .insert_metadata_description(path.clone(), description.clone());
                    }
                }
            }
        }

        // Overwrite the targets entry if specified.
        if let Some(targets_description) = self.state.targets_description()? {
            snapshot_builder = snapshot_builder.insert_metadata_description(
                MetadataPath::from_role(&Role::Targets),
                targets_description,
            );
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

    /// Commit the metadata for this repository without validating it.
    ///
    /// Warning: This can write invalid metadata to a repository without
    /// validating that it is correct.
    #[cfg(test)]
    pub async fn commit_skip_validation(self) -> Result<RawSignedMetadataSet<D>> {
        self.stage_snapshot_if_necessary()?
            .commit_skip_validation()
            .await
    }

    fn need_new_snapshot(&self) -> bool {
        let db = if let Some(ref db) = self.ctx.db {
            db
        } else {
            return true;
        };

        if let Some(snapshot) = db.trusted_snapshot() {
            if snapshot.expires() <= &Utc::now() {
                return true;
            }
        } else {
            return true;
        }

        false
    }
}

impl<'a, D, R> RepoBuilder<'a, D, R, Timestamp<D>>
where
    D: DataInterchange + Sync,
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
                trusted_timestamp.version().checked_add(1).ok_or_else(|| {
                    Error::VerificationFailure(
                        "timestamp version should be less than max u32".into(),
                    )
                })?
            } else {
                1
            }
        } else {
            1
        };

        let description = if let Some(description) = self.state.snapshot_description()? {
            description
        } else {
            self.ctx
                .db
                .and_then(|db| db.trusted_timestamp())
                .map(|timestamp| timestamp.snapshot().clone())
                .ok_or(Error::MissingMetadata(Role::Timestamp))?
        };

        let timestamp_builder = TimestampMetadataBuilder::from_metadata_description(description)
            .version(next_version)
            .expires(Utc::now() + Duration::days(1));

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

    /// Commit the metadata for this repository without validating it.
    ///
    /// Warning: This can write invalid metadata to a repository without
    /// validating that it is correct.
    #[cfg(test)]
    pub async fn commit_skip_validation(self) -> Result<RawSignedMetadataSet<D>> {
        self.stage_timestamp_if_necessary()?
            .commit_skip_validation()
            .await
    }

    fn need_new_timestamp(&self) -> bool {
        let db = if let Some(ref db) = self.ctx.db {
            db
        } else {
            return true;
        };

        if let Some(timestamp) = db.trusted_timestamp() {
            if timestamp.expires() <= &Utc::now() {
                return true;
            }
        } else {
            return true;
        }

        false
    }
}

impl<'a, D, R> RepoBuilder<'a, D, R, Done<D>>
where
    D: DataInterchange + Sync,
    R: RepositoryStorage<D>,
{
    /// Commit the metadata for this repository, then write all metadata to the repository. Before
    /// writing the metadata to `repo`, this will test that a client can update to this metadata to
    /// make sure it is valid.
    pub async fn commit(mut self) -> Result<RawSignedMetadataSet<D>> {
        self.validate_built_metadata()?;
        self.write_repo().await?;
        Ok(self.build_skip_validation())
    }

    /// Commit the metadata for this repository without validating it.
    ///
    /// Warning: This can write invalid metadata to a repository without validating that it is
    /// correct.
    #[cfg(test)]
    pub async fn commit_skip_validation(mut self) -> Result<RawSignedMetadataSet<D>> {
        self.write_repo().await?;
        Ok(self.build_skip_validation())
    }

    /// Build the metadata without validating it for correctness.
    ///
    /// Warning: This can produce invalid metadata.
    fn build_skip_validation(self) -> RawSignedMetadataSet<D> {
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

        builder.build()
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
            return Err(Error::MissingMetadata(Role::Root));
        };

        if let Some(ref timestamp) = self.state.staged_timestamp {
            db.update_timestamp(&timestamp.raw)?;
        }

        if let Some(ref snapshot) = self.state.staged_snapshot {
            db.update_snapshot(&snapshot.raw)?;
        }

        if let Some(ref targets) = self.state.staged_targets {
            db.update_targets(&targets.raw)?;
        }

        Ok(())
    }

    async fn write_repo(&mut self) -> Result<()> {
        let consistent_snapshot = if let Some(ref root) = self.state.staged_root {
            self.ctx
                .repo
                .store_metadata(
                    &MetadataPath::from_role(&Role::Root),
                    MetadataVersion::Number(root.metadata.version()),
                    &mut root.raw.as_bytes(),
                )
                .await?;

            self.ctx
                .repo
                .store_metadata(
                    &MetadataPath::from_role(&Role::Root),
                    MetadataVersion::None,
                    &mut root.raw.as_bytes(),
                )
                .await?;

            root.metadata.consistent_snapshot()
        } else if let Some(db) = self.ctx.db {
            db.trusted_root().consistent_snapshot()
        } else {
            return Err(Error::MissingMetadata(Role::Root));
        };

        if let Some(ref targets) = self.state.staged_targets {
            let path = MetadataPath::from_role(&Role::Targets);
            self.ctx
                .repo
                .store_metadata(&path, MetadataVersion::None, &mut targets.raw.as_bytes())
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
            let path = MetadataPath::from_role(&Role::Snapshot);
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
                    &MetadataPath::from_role(&Role::Timestamp),
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
    use crate::repository::RepositoryProvider;

    use {
        super::*,
        crate::{
            client::{Client, Config},
            crypto::Ed25519PrivateKey,
            interchange::Json,
            metadata::SignedMetadata,
            repository::EphemeralRepository,
        },
        chrono::{
            offset::{TimeZone as _, Utc},
            DateTime,
        },
        futures_executor::block_on,
        futures_util::io::{AsyncReadExt, Cursor},
        lazy_static::lazy_static,
        maplit::hashmap,
        matches::assert_matches,
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
    ) -> SignedMetadata<Json, RootMetadata> {
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
    ) -> SignedMetadata<Json, TargetsMetadata> {
        let targets = TargetsMetadataBuilder::new()
            .version(version)
            .expires(expires)
            .build()
            .unwrap();
        SignedMetadataBuilder::<Json, _>::from_metadata(&targets)
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
        targets: &SignedMetadata<Json, TargetsMetadata>,
        include_length_and_hashes: bool,
    ) -> SignedMetadata<Json, SnapshotMetadata> {
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
            .insert_metadata_description(MetadataPath::from_role(&Role::Targets), description)
            .version(version)
            .expires(expires)
            .build()
            .unwrap();
        SignedMetadataBuilder::<Json, _>::from_metadata(&snapshot)
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
        snapshot: &SignedMetadata<Json, SnapshotMetadata>,
        include_length_and_hashes: bool,
    ) -> SignedMetadata<Json, TimestampMetadata> {
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
        SignedMetadataBuilder::<Json, _>::from_metadata(&timestamp)
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
        metadata: &RawSignedMetadataSet<Json>,
        expected_root: Option<&RawSignedMetadata<Json, RootMetadata>>,
        expected_targets: Option<&RawSignedMetadata<Json, TargetsMetadata>>,
        expected_snapshot: Option<&RawSignedMetadata<Json, SnapshotMetadata>>,
        expected_timestamp: Option<&RawSignedMetadata<Json, TimestampMetadata>>,
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
        repo: &EphemeralRepository<Json>,
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
        let mut remote = EphemeralRepository::<Json>::new();

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
                (
                    MetadataPath::from_role(&Role::Root),
                    MetadataVersion::Number(1),
                ),
                raw_root1.as_bytes(),
            ),
            (
                (MetadataPath::from_role(&Role::Root), MetadataVersion::None),
                raw_root1.as_bytes(),
            ),
            (
                (
                    MetadataPath::from_role(&Role::Targets),
                    MetadataVersion::None,
                ),
                raw_targets1.as_bytes(),
            ),
            (
                (
                    MetadataPath::from_role(&Role::Snapshot),
                    MetadataVersion::None,
                ),
                raw_snapshot1.as_bytes(),
            ),
            (
                (
                    MetadataPath::from_role(&Role::Timestamp),
                    MetadataVersion::None,
                ),
                raw_timestamp1.as_bytes(),
            ),
        ]
        .into_iter()
        .collect();

        if consistent_snapshot {
            expected_metadata.extend(vec![
                (
                    (
                        MetadataPath::from_role(&Role::Targets),
                        MetadataVersion::Number(1),
                    ),
                    raw_targets1.as_bytes(),
                ),
                (
                    (
                        MetadataPath::from_role(&Role::Snapshot),
                        MetadataVersion::Number(1),
                    ),
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
                (
                    MetadataPath::from_role(&Role::Root),
                    MetadataVersion::Number(2),
                ),
                raw_root2.as_bytes(),
            ),
            (
                (MetadataPath::from_role(&Role::Root), MetadataVersion::None),
                raw_root2.as_bytes(),
            ),
            (
                (
                    MetadataPath::from_role(&Role::Targets),
                    MetadataVersion::None,
                ),
                raw_targets2.as_bytes(),
            ),
            (
                (
                    MetadataPath::from_role(&Role::Snapshot),
                    MetadataVersion::None,
                ),
                raw_snapshot2.as_bytes(),
            ),
            (
                (
                    MetadataPath::from_role(&Role::Timestamp),
                    MetadataVersion::None,
                ),
                raw_timestamp2.as_bytes(),
            ),
        ]);

        if consistent_snapshot {
            expected_metadata.extend(vec![
                (
                    (
                        MetadataPath::from_role(&Role::Targets),
                        MetadataVersion::Number(2),
                    ),
                    raw_targets2.as_bytes(),
                ),
                (
                    (
                        MetadataPath::from_role(&Role::Snapshot),
                        MetadataVersion::Number(2),
                    ),
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
        let mut repo = EphemeralRepository::<Json>::new();
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
        let mut repo = EphemeralRepository::<Json>::new();

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
            let mut repo = EphemeralRepository::<Json>::new();
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
                    .commit().await,
                Err(Error::VerificationFailure(s))
                if &s == "Attempted to roll back root metadata at version 1 to 3."
            );
        })
    }

    #[test]
    fn test_add_target_not_consistent_snapshot() {
        block_on(async move {
            let mut repo = EphemeralRepository::<Json>::new();

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
                .file_hash_algorithms(hash_algs)
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
            let mut repo = EphemeralRepository::<Json>::new();

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
                .file_hash_algorithms(hash_algs)
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
            let mut remote = EphemeralRepository::<Json>::new();

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
                    (
                        MetadataPath::from_role(&Role::Root),
                        MetadataVersion::Number(1),
                    ),
                    metadata1.root().unwrap().as_bytes(),
                ),
                (
                    (MetadataPath::from_role(&Role::Root), MetadataVersion::None),
                    metadata1.root().unwrap().as_bytes(),
                ),
                (
                    (
                        MetadataPath::from_role(&Role::Targets),
                        MetadataVersion::Number(1),
                    ),
                    metadata1.targets().unwrap().as_bytes(),
                ),
                (
                    (
                        MetadataPath::from_role(&Role::Targets),
                        MetadataVersion::None,
                    ),
                    metadata1.targets().unwrap().as_bytes(),
                ),
                (
                    (
                        MetadataPath::from_role(&Role::Snapshot),
                        MetadataVersion::Number(1),
                    ),
                    metadata1.snapshot().unwrap().as_bytes(),
                ),
                (
                    (
                        MetadataPath::from_role(&Role::Snapshot),
                        MetadataVersion::None,
                    ),
                    metadata1.snapshot().unwrap().as_bytes(),
                ),
                (
                    (
                        MetadataPath::from_role(&Role::Timestamp),
                        MetadataVersion::None,
                    ),
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
                        (
                            MetadataPath::from_role(&Role::Targets),
                            MetadataVersion::Number(2),
                        ),
                        metadata2.targets().unwrap().as_bytes(),
                    ),
                    (
                        (
                            MetadataPath::from_role(&Role::Targets),
                            MetadataVersion::None,
                        ),
                        metadata2.targets().unwrap().as_bytes(),
                    ),
                    (
                        (
                            MetadataPath::from_role(&Role::Snapshot),
                            MetadataVersion::Number(2),
                        ),
                        metadata2.snapshot().unwrap().as_bytes(),
                    ),
                    (
                        (
                            MetadataPath::from_role(&Role::Snapshot),
                            MetadataVersion::None,
                        ),
                        metadata2.snapshot().unwrap().as_bytes(),
                    ),
                    (
                        (
                            MetadataPath::from_role(&Role::Timestamp),
                            MetadataVersion::None,
                        ),
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
                        (
                            MetadataPath::from_role(&Role::Snapshot),
                            MetadataVersion::Number(3),
                        ),
                        metadata3.snapshot().unwrap().as_bytes(),
                    ),
                    (
                        (
                            MetadataPath::from_role(&Role::Snapshot),
                            MetadataVersion::None,
                        ),
                        metadata3.snapshot().unwrap().as_bytes(),
                    ),
                    (
                        (
                            MetadataPath::from_role(&Role::Timestamp),
                            MetadataVersion::None,
                        ),
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
                    (
                        MetadataPath::from_role(&Role::Timestamp),
                        MetadataVersion::None,
                    ),
                    metadata4.timestamp().unwrap().as_bytes(),
                )]
                .into_iter(),
            );

            assert_repo(&remote, &expected_metadata);
        })
    }
}
