// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    std::{
        iter::{IntoIterator, Iterator},
        sync::Arc,
    },
    tuf::{
        client::{Client, Config, PathTranslator},
        crypto::{HashAlgorithm, PrivateKey},
        interchange::DataInterchange,
        metadata::{
            Metadata, MetadataPath, MetadataVersion, RawSignedMetadata, Role, RootMetadata,
            RootMetadataBuilder, SignedMetadataBuilder, SnapshotMetadata, SnapshotMetadataBuilder,
            TargetsMetadata, TargetsMetadataBuilder, TimestampMetadataBuilder,
        },
        repository::{EphemeralRepository, RepositoryProvider, RepositoryStorage},
    },
};

struct RepoBuilderState<'a, D, R, T>
where
    D: DataInterchange + Sync,
    R: RepositoryProvider<D> + RepositoryStorage<D>,
    T: PathTranslator + Clone,
{
    repo: Arc<R>,
    client: Option<Client<D, EphemeralRepository<D>, Arc<R>, T>>,
    config: Config<T>,
    hash_algorithms: Vec<HashAlgorithm>,
    root_keys: Vec<&'a dyn PrivateKey>,
    snapshot_keys: Vec<&'a dyn PrivateKey>,
    timestamp_keys: Vec<&'a dyn PrivateKey>,
}

pub struct RepoBuilder<'a, D, R, T>
where
    D: DataInterchange + Sync,
    R: RepositoryProvider<D> + RepositoryStorage<D>,
    T: PathTranslator + Clone,
{
    state: RepoBuilderState<'a, D, R, T>,
    target_keys: Vec<&'a dyn PrivateKey>,
}

pub struct RepoBuilderWithRoot<'a, D, R, T>
where
    D: DataInterchange + Sync,
    R: RepositoryProvider<D> + RepositoryStorage<D>,
    T: PathTranslator + Clone,
{
    state: RepoBuilderState<'a, D, R, T>,
    root: RootMetadata,
    target_keys: Vec<&'a dyn PrivateKey>,
}

pub struct RepoBuilderWithTargets<'a, D, R, T>
where
    D: DataInterchange + Sync,
    R: RepositoryProvider<D> + RepositoryStorage<D>,
    T: PathTranslator + Clone,
{
    state: RepoBuilderState<'a, D, R, T>,
    store_root: bool,
    root: RootMetadata,
    targets: TargetsMetadata,
    target_keys: Vec<&'a dyn PrivateKey>,
}

pub struct RepoBuilderWithSnapshot<'a, D, R, T>
where
    D: DataInterchange + Sync,
    R: RepositoryProvider<D> + RepositoryStorage<D>,
    T: PathTranslator + Clone,
{
    state: RepoBuilderState<'a, D, R, T>,
    store_root: bool,
    root: RootMetadata,
    targets_version: u32,
    raw_targets: RawSignedMetadata<D, TargetsMetadata>,
    snapshot: SnapshotMetadata,
}

impl<'a, D, R, T> RepoBuilder<'a, D, R, T>
where
    D: DataInterchange + Sync,
    R: RepositoryProvider<D> + RepositoryStorage<D>,
    T: PathTranslator + Clone,
{
    /// Create a builder on top of an uninitialized repository that, when committed, will populate
    /// it with new initial metadata.
    pub fn create<K, L, M, N, P: 'a>(
        config: Config<T>,
        repo: R,
        root_keys: K,
        target_keys: L,
        snapshot_keys: M,
        timestamp_keys: N,
    ) -> RepoBuilder<'a, D, R, T>
    where
        K: IntoIterator<Item = &'a P>,
        K::IntoIter: 'a,
        L: IntoIterator<Item = &'a P>,
        L::IntoIter: 'a,
        M: IntoIterator<Item = &'a P>,
        M::IntoIter: 'a,
        N: IntoIterator<Item = &'a P>,
        N::IntoIter: 'a,
        P: PrivateKey,
        D: 'a,
    {
        let repo = Arc::new(repo);
        RepoBuilder {
            state: RepoBuilderState {
                repo,
                client: None,
                config,
                hash_algorithms: vec![HashAlgorithm::Sha256],
                root_keys: root_keys.into_iter().map(|x| x as &dyn PrivateKey).collect(),
                snapshot_keys: snapshot_keys.into_iter().map(|x| x as &dyn PrivateKey).collect(),
                timestamp_keys: timestamp_keys.into_iter().map(|x| x as &dyn PrivateKey).collect(),
            },
            target_keys: target_keys.into_iter().map(|x| x as &dyn PrivateKey).collect(),
        }
    }

    /// Create a new RepoBuilder, which will commit changes to this repository.
    pub async fn with_trusted_root<K, L, M, N, P: 'a>(
        config: Config<T>,
        trusted_root: &'a RawSignedMetadata<D, RootMetadata>,
        repo: R,
        root_keys: K,
        target_keys: L,
        snapshot_keys: M,
        timestamp_keys: N,
    ) -> Result<RepoBuilder<'a, D, R, T>>
    where
        K: IntoIterator<Item = &'a P>,
        K::IntoIter: 'a,
        L: IntoIterator<Item = &'a P>,
        L::IntoIter: 'a,
        M: IntoIterator<Item = &'a P>,
        M::IntoIter: 'a,
        N: IntoIterator<Item = &'a P>,
        N::IntoIter: 'a,
        P: PrivateKey,
    {
        let repo = Arc::new(repo);
        let mut client = Client::with_trusted_root(
            config.clone(),
            trusted_root,
            EphemeralRepository::new(),
            Arc::clone(&repo),
        )
        .await?;
        client.update().await?;
        Ok(RepoBuilder {
            state: RepoBuilderState {
                repo,
                client: Some(client),
                config,
                hash_algorithms: vec![HashAlgorithm::Sha256],
                root_keys: root_keys.into_iter().map(|x| x as &dyn PrivateKey).collect(),
                snapshot_keys: snapshot_keys.into_iter().map(|x| x as &dyn PrivateKey).collect(),
                timestamp_keys: timestamp_keys.into_iter().map(|x| x as &dyn PrivateKey).collect(),
            },
            target_keys: target_keys.into_iter().map(|x| x as &dyn PrivateKey).collect(),
        })
    }

    /// Change which hash algorithms to use with this snapshot.
    pub fn with_hash_algorithms<K>(mut self, algorithms: K) -> Self
    where
        K: IntoIterator<Item = HashAlgorithm>,
    {
        self.state.hash_algorithms = algorithms.into_iter().collect();
        self
    }

    fn make_with_root(self, root: RootMetadata) -> RepoBuilderWithRoot<'a, D, R, T> {
        RepoBuilderWithRoot { root, target_keys: self.target_keys, state: self.state }
    }

    /// Check whether we have new keys and thus need to rebuild the root
    async fn need_key_refresh(&self) -> Result<bool> {
        let client = if let Some(client) = self.state.client.as_ref() {
            client
        } else {
            return Ok(true);
        };

        let root_keys_count = client.trusted_root().root_keys().count();

        if root_keys_count != self.state.root_keys.len() {
            return Ok(true);
        }

        for &key in &self.state.root_keys {
            if client.trusted_root().root_keys().find(|&x| x == key.public()).is_none() {
                return Ok(true);
            }
        }

        for &key in &self.target_keys {
            if client.trusted_root().targets_keys().find(|&x| x == key.public()).is_none() {
                return Ok(true);
            }
        }

        for &key in &self.state.snapshot_keys {
            if client.trusted_root().snapshot_keys().find(|&x| x == key.public()).is_none() {
                return Ok(true);
            }
        }

        for &key in &self.state.timestamp_keys {
            if client.trusted_root().timestamp_keys().find(|&x| x == key.public()).is_none() {
                return Ok(true);
            }
        }

        Ok(false)
    }

    /// This function will generate a new root metadata, which will automatically
    /// increment the version, and set the expiration to the default expiration of
    /// one year.
    pub async fn with_root_builder<F>(self, f: F) -> Result<RepoBuilderWithRoot<'a, D, R, T>>
    where
        F: FnOnce(RootMetadataBuilder) -> RootMetadataBuilder,
    {
        let mut builder = RootMetadataBuilder::new().version(
            self.state.client.as_ref().map(|client| client.root_version()).unwrap_or(0) + 1,
        );

        if let Some(consistent_snapshot) =
            self.state.client.as_ref().map(|client| client.trusted_root().consistent_snapshot())
        {
            builder = builder.consistent_snapshot(consistent_snapshot);
        }

        for key in &self.state.root_keys {
            builder = builder.root_key(key.public().clone());
        }

        for key in &self.target_keys {
            builder = builder.targets_key(key.public().clone());
        }

        for key in &self.state.snapshot_keys {
            builder = builder.snapshot_key(key.public().clone());
        }

        for key in &self.state.timestamp_keys {
            builder = builder.timestamp_key(key.public().clone());
        }

        Ok(self.make_with_root(f(builder).build()?))
    }

    /// This function will generate a new targets metadata, which will
    /// automatically increment the version, and set the expiration to the
    /// default expiration of 3 months.
    pub async fn with_targets_builder<F>(self, f: F) -> Result<RepoBuilderWithTargets<'a, D, R, T>>
    where
        F: FnOnce(TargetsMetadataBuilder) -> TargetsMetadataBuilder,
    {
        if self.need_key_refresh().await? {
            self.with_root_builder(|x| x).await?.with_targets_builder(f).await
        } else {
            // need_key_refresh should always be true when we're creating a repo from scratch,
            // which means there's always a client here.
            let root = (**self.state.client.as_ref().unwrap().trusted_root()).clone();
            self.make_with_root(root).with_targets_builder_store_root(f, false).await
        }
    }

    /// If keys have changed, rewrite the root metadata. Otherwise do nothing.
    pub async fn commit(self) -> Result<()> {
        if self.need_key_refresh().await? {
            self.with_root_builder(|x| x).await?.commit().await
        } else {
            Ok(())
        }
    }
}

impl<'a, D, R, T> RepoBuilderWithRoot<'a, D, R, T>
where
    D: DataInterchange + Sync,
    R: RepositoryProvider<D> + RepositoryStorage<D>,
    T: PathTranslator + Clone,
{
    /// This function will generate a new targets metadata, which will
    /// automatically increment the version, and set the expiration to the
    /// default expiration of 3 months.
    pub async fn with_targets_builder<F>(self, f: F) -> Result<RepoBuilderWithTargets<'a, D, R, T>>
    where
        F: FnOnce(TargetsMetadataBuilder) -> TargetsMetadataBuilder,
    {
        self.with_targets_builder_store_root(f, true).await
    }

    async fn with_targets_builder_store_root<F>(
        self,
        f: F,
        store_root: bool,
    ) -> Result<RepoBuilderWithTargets<'a, D, R, T>>
    where
        F: FnOnce(TargetsMetadataBuilder) -> TargetsMetadataBuilder,
    {
        let targets = f(TargetsMetadataBuilder::new().version(
            self.state.client.as_ref().and_then(|client| client.targets_version()).unwrap_or(0) + 1,
        ))
        .build()?;
        Ok(RepoBuilderWithTargets {
            state: self.state,
            root: self.root,
            store_root,
            targets,
            target_keys: self.target_keys,
        })
    }

    /// Write the metadata to the repository. Before writing the metadata to `repo`,
    /// this will test that a client can update to this metadata to make sure it
    /// is valid.
    pub async fn commit(self) -> Result<()> {
        self.with_targets_builder(|x| x).await?.commit().await
    }
}

impl<'a, D, R, T> RepoBuilderWithTargets<'a, D, R, T>
where
    D: DataInterchange + Sync,
    R: RepositoryProvider<D> + RepositoryStorage<D>,
    T: PathTranslator + Clone,
{
    /// This function will generate a new snapshot metadata, which will automatically
    /// increment the version, and set the expiration to the default expiration of
    /// 7 days.
    pub async fn with_snapshot_builder<F>(
        self,
        f: F,
    ) -> Result<RepoBuilderWithSnapshot<'a, D, R, T>>
    where
        F: FnOnce(SnapshotMetadataBuilder) -> SnapshotMetadataBuilder,
    {
        let mut signed_builder = SignedMetadataBuilder::<D, _>::from_metadata(&self.targets)?;
        for key in self.target_keys {
            signed_builder = signed_builder.sign(key)?;
        }
        let signed_targets = signed_builder.build();
        let raw_targets = signed_targets.to_raw()?;
        let snapshot = f(SnapshotMetadataBuilder::new()
            .version(
                self.state
                    .client
                    .as_ref()
                    .and_then(|client| client.snapshot_version())
                    .unwrap_or(0)
                    + 1,
            )
            .insert_metadata(&signed_targets, &self.state.hash_algorithms)?)
        .build()?;
        Ok(RepoBuilderWithSnapshot {
            state: self.state,
            root: self.root,
            store_root: self.store_root,
            targets_version: self.targets.version(),
            raw_targets,
            snapshot,
        })
    }

    /// Write the metadata to the repository. Before writing the metadata to `repo`,
    /// this will test that a client can update to this metadata to make sure it
    /// is valid.
    pub async fn commit(self) -> Result<()> {
        self.with_snapshot_builder(|x| x).await?.commit().await
    }
}

impl<'a, D, R, T> RepoBuilderWithSnapshot<'a, D, R, T>
where
    D: DataInterchange + Sync,
    R: RepositoryProvider<D> + RepositoryStorage<D>,
    T: PathTranslator + Clone,
{
    /// Write the metadata to the repository. Before writing the metadata to `repo`,
    /// this will test that a client can update to this metadata to make sure it
    /// is valid.
    pub async fn commit(self) -> Result<()> {
        self.build_timestamp_and_commit(|x| x).await
    }

    /// Build timestamp metadata for this repository, then write all metadata to
    /// the repository. Before writing the metadata to `repo`, this will test
    /// that a client can update to this metadata to make sure it is valid.
    pub async fn build_timestamp_and_commit<F>(self, f: F) -> Result<()>
    where
        F: FnOnce(TimestampMetadataBuilder) -> TimestampMetadataBuilder,
    {
        let repo = Arc::clone(&self.state.repo);

        if self.state.client.is_some() {
            let staging_repo = EphemeralRepository::<D>::new();
            let snapshot_version = self.snapshot.version();
            let raw_targets = self.raw_targets.clone();
            let targets_version = self.targets_version;
            let config = self.state.config.clone();
            let (root, raw_snapshot) = self.write_repo(f, &staging_repo).await?;

            Client::with_trusted_local(config, &repo, staging_repo).await?.update().await?;

            // The update won't pull the consistent metadata from the staging repo to the final
            // repo, so we have to write it manually.
            if root.consistent_snapshot() {
                repo.store_metadata(
                    &MetadataPath::from_role(&Role::Targets),
                    &MetadataVersion::Number(targets_version),
                    &mut raw_targets.as_bytes(),
                )
                .await?;

                repo.store_metadata(
                    &MetadataPath::from_role(&Role::Snapshot),
                    &MetadataVersion::Number(snapshot_version),
                    &mut raw_snapshot.as_bytes(),
                )
                .await?;
            }
        } else {
            let repo = Arc::clone(&self.state.repo);
            self.write_repo(f, repo).await?;
        }

        Ok(())
    }

    async fn write_repo<F, S>(
        self,
        f: F,
        repo: S,
    ) -> Result<(RootMetadata, RawSignedMetadata<D, SnapshotMetadata>)>
    where
        F: FnOnce(TimestampMetadataBuilder) -> TimestampMetadataBuilder,
        S: RepositoryProvider<D> + RepositoryStorage<D>,
    {
        let root = self.root;

        if self.store_root {
            let mut signed_builder = SignedMetadataBuilder::<D, _>::from_metadata(&root)?;
            for key in self.state.root_keys {
                signed_builder = signed_builder.sign(key)?;
            }
            let raw_root = signed_builder.build().to_raw()?;

            repo.store_metadata(
                &MetadataPath::from_role(&Role::Root),
                &MetadataVersion::Number(root.version()),
                &mut raw_root.as_bytes(),
            )
            .await?;
        }

        let mut signed_builder = SignedMetadataBuilder::<D, _>::from_metadata(&self.snapshot)?;
        for key in self.state.snapshot_keys {
            signed_builder = signed_builder.sign(key)?;
        }
        let signed_snapshot = signed_builder.build();
        let raw_snapshot = signed_snapshot.to_raw()?;

        let timestamp = f(TimestampMetadataBuilder::from_snapshot(
            &signed_snapshot,
            &self.state.hash_algorithms,
        )?
        .version(
            self.state.client.as_ref().and_then(|client| client.timestamp_version()).unwrap_or(0)
                + 1,
        ))
        .build()?;

        let mut signed_builder = SignedMetadataBuilder::<D, _>::from_metadata(&timestamp)?;
        for key in self.state.timestamp_keys {
            signed_builder = signed_builder.sign(key)?;
        }
        let signed_timestamp = signed_builder.build();
        let raw_timestamp = signed_timestamp.to_raw()?;

        if root.consistent_snapshot() {
            repo.store_metadata(
                &MetadataPath::from_role(&Role::Targets),
                &MetadataVersion::Number(self.targets_version),
                &mut self.raw_targets.as_bytes(),
            )
            .await?;

            repo.store_metadata(
                &MetadataPath::from_role(&Role::Snapshot),
                &MetadataVersion::Number(self.snapshot.version()),
                &mut raw_snapshot.as_bytes(),
            )
            .await?;
        }

        repo.store_metadata(
            &MetadataPath::from_role(&Role::Targets),
            &MetadataVersion::None,
            &mut self.raw_targets.as_bytes(),
        )
        .await?;

        repo.store_metadata(
            &MetadataPath::from_role(&Role::Snapshot),
            &MetadataVersion::None,
            &mut raw_snapshot.as_bytes(),
        )
        .await?;

        repo.store_metadata(
            &MetadataPath::from_role(&Role::Timestamp),
            &MetadataVersion::None,
            &mut raw_timestamp.as_bytes(),
        )
        .await?;

        Ok((root, raw_snapshot))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use {
        chrono::offset::{TimeZone as _, Utc},
        fuchsia_async::{self as fasync, futures::AsyncReadExt as _},
        lazy_static::lazy_static,
        matches::assert_matches,
        std::iter::once,
        tuf::{crypto::Ed25519PrivateKey, interchange::Json},
    };

    lazy_static! {
        static ref KEYS: Vec<Ed25519PrivateKey> = {
            let keys: &[&[u8]] = &[
                include_bytes!("../test_data/ed25519-1.pk8.der"),
                include_bytes!("../test_data/ed25519-2.pk8.der"),
                include_bytes!("../test_data/ed25519-3.pk8.der"),
                include_bytes!("../test_data/ed25519-4.pk8.der"),
                include_bytes!("../test_data/ed25519-5.pk8.der"),
                include_bytes!("../test_data/ed25519-6.pk8.der"),
            ];
            keys.iter().map(|b| Ed25519PrivateKey::from_pkcs8(b).unwrap()).collect()
        };
    }

    #[fasync::run_singlethreaded(test)]
    async fn root_chain_update() {
        do_root_chain_update(false).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn root_chain_update_consistent() {
        do_root_chain_update(true).await;
    }

    async fn do_root_chain_update(consistent_snapshot: bool) {
        let repo = EphemeralRepository::<Json>::new();

        // First, create the initial metadata.
        RepoBuilder::create(
            Config::default(),
            &repo,
            [&KEYS[0]],
            [&KEYS[0], &KEYS[1], &KEYS[2]],
            [&KEYS[0], &KEYS[1], &KEYS[2]],
            [&KEYS[0], &KEYS[1], &KEYS[2]],
        )
        .with_root_builder(|root_builder| {
            root_builder
                .expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
                .consistent_snapshot(consistent_snapshot)
        })
        .await
        .unwrap()
        .commit()
        .await
        .unwrap();

        let client_repo = EphemeralRepository::new();
        let mut client = Client::with_trusted_root_keys(
            Config::default(),
            &MetadataVersion::Number(1),
            1,
            once(&KEYS[0].public().clone()),
            &client_repo,
            &repo,
        )
        .await
        .unwrap();

        let raw_root = {
            let mut reader = repo
                .fetch_metadata(
                    &MetadataPath::from_role(&Role::Root),
                    &MetadataVersion::Number(1),
                    None,
                    None,
                )
                .await
                .unwrap();
            let mut buf = Vec::new();
            reader.read_to_end(&mut buf).await.unwrap();
            RawSignedMetadata::new(buf)
        };

        assert_matches!(client.update().await, Ok(true));
        assert_eq!(client.trusted_root().version(), 1);

        // Another update should not fetch anything.
        assert_matches!(client.update().await, Ok(false));
        assert_eq!(client.trusted_root().version(), 1);

        ////
        // Now bump the root to version 3

        // Make sure the version 2 is also signed by version 1's keys.
        RepoBuilder::with_trusted_root(
            Config::default(),
            &raw_root,
            &repo,
            [&KEYS[0], &KEYS[1]],
            [&KEYS[0], &KEYS[1], &KEYS[2]],
            [&KEYS[0], &KEYS[1], &KEYS[2]],
            [&KEYS[0], &KEYS[1], &KEYS[2]],
        )
        .await
        .unwrap()
        .commit()
        .await
        .unwrap();

        RepoBuilder::with_trusted_root(
            Config::default(),
            &raw_root,
            &repo,
            [&KEYS[1], &KEYS[2]],
            [&KEYS[0], &KEYS[1], &KEYS[2]],
            [&KEYS[0], &KEYS[1], &KEYS[2]],
            [&KEYS[0], &KEYS[1], &KEYS[2]],
        )
        .await
        .unwrap()
        .commit()
        .await
        .unwrap();

        ////
        // Finally, check that the update brings us to version 3.

        assert_matches!(client.update().await, Ok(true));
        assert_eq!(client.trusted_root().version(), 3);
    }
}
