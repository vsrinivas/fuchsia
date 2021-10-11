use crate::{
    crypto::{HashAlgorithm, PrivateKey},
    interchange::DataInterchange,
    metadata::{
        Metadata, MetadataPath, MetadataVersion, RawSignedMetadata, Role, RootMetadata,
        RootMetadataBuilder, SignedMetadataBuilder, SnapshotMetadata, SnapshotMetadataBuilder,
        TargetsMetadata, TargetsMetadataBuilder, TimestampMetadata, TimestampMetadataBuilder,
    },
    repository::{Repository, RepositoryStorage},
    Result,
};

// This helper builder simplifies the process of creating new metadata.
//
// FIXME: This is not ready yet for public use, it is only intended for internal testing until the
// design is complete.
pub(crate) struct RepoBuilder<'a, R, D>
where
    R: RepositoryStorage<D> + Sync,
    D: DataInterchange + Sync,
{
    repo: Repository<R, D>,
    root_keys: Vec<&'a dyn PrivateKey>,
    targets_keys: Vec<&'a dyn PrivateKey>,
    snapshot_keys: Vec<&'a dyn PrivateKey>,
    timestamp_keys: Vec<&'a dyn PrivateKey>,
    snapshot_version: u32,
    timestamp_version: u32,
    root_builder: RootMetadataBuilder,
    targets_builder: Option<TargetsMetadataBuilder>,
}

impl<'a, R, D> RepoBuilder<'a, R, D>
where
    R: RepositoryStorage<D> + Sync,
    D: DataInterchange + Sync,
{
    pub(crate) fn new(repo: R) -> Self {
        let repo = Repository::new(repo);

        Self {
            repo,
            root_keys: vec![],
            targets_keys: vec![],
            snapshot_keys: vec![],
            timestamp_keys: vec![],
            snapshot_version: 1,
            timestamp_version: 1,
            root_builder: RootMetadataBuilder::new(),
            targets_builder: None,
        }
    }

    pub(crate) fn root_keys(mut self, keys: Vec<&'a dyn PrivateKey>) -> Self {
        self.root_keys = keys;
        self
    }

    pub(crate) fn targets_keys(mut self, keys: Vec<&'a dyn PrivateKey>) -> Self {
        self.targets_keys = keys;
        self
    }

    pub(crate) fn snapshot_keys(mut self, keys: Vec<&'a dyn PrivateKey>) -> Self {
        self.snapshot_keys = keys;
        self
    }

    pub(crate) fn timestamp_keys(mut self, keys: Vec<&'a dyn PrivateKey>) -> Self {
        self.timestamp_keys = keys;
        self
    }

    pub(crate) fn targets_version(self, version: u32) -> Self {
        self.with_targets_builder(|bld| bld.version(version))
    }

    pub(crate) fn snapshot_version(mut self, version: u32) -> Self {
        self.snapshot_version = version;
        self
    }

    pub(crate) fn timestamp_version(mut self, version: u32) -> Self {
        self.timestamp_version = version;
        self
    }

    pub(crate) fn with_root_builder<F>(mut self, f: F) -> Self
    where
        F: FnOnce(RootMetadataBuilder) -> RootMetadataBuilder,
    {
        self.root_builder = f(self.root_builder);
        self
    }

    pub(crate) fn with_targets_builder<F>(mut self, f: F) -> Self
    where
        F: FnOnce(TargetsMetadataBuilder) -> TargetsMetadataBuilder,
    {
        let targets_builder = self
            .targets_builder
            .unwrap_or_else(TargetsMetadataBuilder::new);
        self.targets_builder = Some(f(targets_builder));
        self
    }

    pub(crate) async fn commit(mut self) -> Result<CommittedMetadata<D>> {
        let root = self.root_builder.build()?;
        self.root_builder = RootMetadataBuilder::from(root.clone());

        let mut signed_builder = SignedMetadataBuilder::from_metadata(&root)?;
        for key in &self.root_keys {
            signed_builder = signed_builder.sign(*key)?;
        }
        let raw_root = signed_builder.build().to_raw()?;

        self.repo
            .store_metadata(
                &MetadataPath::from_role(&Role::Root),
                &MetadataVersion::Number(root.version()),
                &raw_root,
            )
            .await?;

        let (targets, snapshot, timestamp) =
            if let Some(targets_builder) = self.targets_builder.take() {
                let targets = targets_builder.build()?;

                let mut signed_builder = SignedMetadataBuilder::from_metadata(&targets)?;
                for key in &self.targets_keys {
                    signed_builder = signed_builder.sign(*key)?;
                }
                let signed_targets = signed_builder.build();
                let raw_targets = signed_targets.to_raw()?;

                let snapshot = SnapshotMetadataBuilder::new()
                    .version(self.snapshot_version)
                    .insert_metadata(&signed_targets, &[HashAlgorithm::Sha256])?
                    .build()?;

                let mut signed_builder = SignedMetadataBuilder::from_metadata(&snapshot)?;
                for key in &self.snapshot_keys {
                    signed_builder = signed_builder.sign(*key)?;
                }
                let signed_snapshot = signed_builder.build();
                let raw_snapshot = signed_snapshot.to_raw()?;

                let timestamp = TimestampMetadataBuilder::from_snapshot(
                    &signed_snapshot,
                    &[HashAlgorithm::Sha256],
                )?
                .version(self.timestamp_version)
                .build()?;

                let mut signed_builder = SignedMetadataBuilder::from_metadata(&timestamp)?;
                for key in &self.timestamp_keys {
                    signed_builder = signed_builder.sign(*key)?;
                }
                let signed_timestamp = signed_builder.build();
                let raw_timestamp = signed_timestamp.to_raw()?;

                if root.consistent_snapshot() {
                    self.repo
                        .store_metadata(
                            &MetadataPath::from_role(&Role::Targets),
                            &MetadataVersion::Number(targets.version()),
                            &raw_targets,
                        )
                        .await?;

                    self.repo
                        .store_metadata(
                            &MetadataPath::from_role(&Role::Snapshot),
                            &MetadataVersion::Number(snapshot.version()),
                            &raw_snapshot,
                        )
                        .await?;
                }

                self.repo
                    .store_metadata(
                        &MetadataPath::from_role(&Role::Targets),
                        &MetadataVersion::None,
                        &raw_targets,
                    )
                    .await?;

                self.repo
                    .store_metadata(
                        &MetadataPath::from_role(&Role::Snapshot),
                        &MetadataVersion::None,
                        &raw_snapshot,
                    )
                    .await?;

                self.repo
                    .store_metadata(
                        &MetadataPath::from_role(&Role::Timestamp),
                        &MetadataVersion::None,
                        &raw_timestamp,
                    )
                    .await?;

                (Some(raw_targets), Some(raw_snapshot), Some(raw_timestamp))
            } else {
                (None, None, None)
            };

        Ok(CommittedMetadata {
            root: raw_root,
            targets,
            snapshot,
            timestamp,
        })
    }
}

pub(crate) struct CommittedMetadata<D> {
    pub(crate) root: RawSignedMetadata<D, RootMetadata>,
    pub(crate) targets: Option<RawSignedMetadata<D, TargetsMetadata>>,
    pub(crate) snapshot: Option<RawSignedMetadata<D, SnapshotMetadata>>,
    pub(crate) timestamp: Option<RawSignedMetadata<D, TimestampMetadata>>,
}
