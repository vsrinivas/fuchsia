// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Result};
use futures::AsyncReadExt;
use std::collections::HashMap;
use std::default::Default;
use std::{fs, path};
use tuf::crypto::{Ed25519PrivateKey, HashAlgorithm, PrivateKey, PublicKey};
use tuf::interchange::{DataInterchange, Json};
use tuf::metadata::{
    Metadata, MetadataDescription, MetadataPath, MetadataVersion, Role as TufRole, RootMetadata,
    RootMetadataBuilder, SignedMetadata, SnapshotMetadata, SnapshotMetadataBuilder,
    TargetDescription, TargetPath, TargetsMetadata, TimestampMetadata, TimestampMetadataBuilder,
};
use tuf::repository::{
    FileSystemRepository, FileSystemRepositoryBuilder, RepositoryProvider, RepositoryStorage,
};

type TargetsMap = HashMap<TargetPath, TargetDescription>;

pub struct TufRepo {
    repo: FileSystemRepository<Json>,
    targets: TargetsMap,
    keys_dir: path::PathBuf,
    keys: [Option<Box<dyn PrivateKey>>; 4],
    versions: [u32; 4],
}

static HASH_ALGS: &[HashAlgorithm] = &[HashAlgorithm::Sha512];

impl TufRepo {
    pub fn new(repo_dir: path::PathBuf, keys_dir: path::PathBuf) -> Result<TufRepo> {
        let needs_init = !repo_dir.exists();
        let repo = FileSystemRepositoryBuilder::new(repo_dir).build()?;
        let mut repo = TufRepo {
            repo,
            targets: TargetsMap::default(),
            keys_dir,
            keys: Default::default(),
            versions: Default::default(),
        };
        if needs_init {
            repo.initialize()?;
        }
        repo.load_state()?;
        Ok(repo)
    }

    fn initialize(&mut self) -> Result<()> {
        fs::create_dir_all(&self.keys_dir)?;

        self.gen_key(Role::Root)?;
        self.gen_key(Role::Targets)?;
        self.gen_key(Role::Snapshot)?;
        self.gen_key(Role::Timestamp)?;

        let root = RootMetadataBuilder::new()
            .root_key(self.public_key(Role::Root).clone())
            .snapshot_key(self.public_key(Role::Snapshot).clone())
            .targets_key(self.public_key(Role::Targets).clone())
            .timestamp_key(self.public_key(Role::Timestamp).clone())
            .build()?;
        self.store_metadata(root)?;

        // Commit empty targets.
        self.commit_targets()?;

        Ok(())
    }

    fn load_state(&mut self) -> Result<()> {
        self.targets = self.load_metadata::<TargetsMetadata>()?.targets().clone();

        fn load_version<M: Metadata>(repo: &mut TufRepo) -> Result<()> {
            repo.versions[Role::from(M::ROLE) as usize] = repo.load_metadata::<M>()?.version();
            Ok(())
        }
        load_version::<RootMetadata>(self)?;
        load_version::<TargetsMetadata>(self)?;
        load_version::<SnapshotMetadata>(self)?;
        load_version::<TimestampMetadata>(self)?;

        Ok(())
    }

    fn gen_key(&self, role: Role) -> Result<()> {
        let key = ring::signature::Ed25519KeyPair::generate_pkcs8(&ring::rand::SystemRandom::new())
            .map_err(|_| anyhow!("failed to generate keypair"))?;
        fs::write(self.keys_dir.join(role.key_filename()), &key)?;
        Ok(())
    }

    fn public_key(&mut self, role: Role) -> &PublicKey {
        self.private_key(role).public()
    }
    fn private_key(&mut self, role: Role) -> &dyn PrivateKey {
        let Self { ref mut keys, ref keys_dir, .. } = self;
        &**keys[role as usize].get_or_insert_with(|| {
            Box::new(
                Ed25519PrivateKey::from_pkcs8(
                    &fs::read(keys_dir.join(role.key_filename())).unwrap(),
                )
                .unwrap(),
            )
        })
    }

    pub fn commit_targets(&mut self) -> Result<()> {
        let targets = self.store_metadata(TargetsMetadata::new(
            self.next_version(Role::Targets),
            chrono::offset::Utc::now() + chrono::Duration::days(30),
            self.targets.clone(),
            None,
        )?)?;
        let snapshots = self.store_metadata(
            SnapshotMetadataBuilder::new()
                .version(self.next_version(Role::Snapshot))
                .insert_metadata_description(Role::Targets.path(), targets)
                .insert_metadata(&self.load_signed_metadata::<RootMetadata>()?, HASH_ALGS)?
                .build()?,
        )?;
        self.store_metadata(
            TimestampMetadataBuilder::from_metadata_description(snapshots)
                .version(self.next_version(Role::Timestamp))
                .build()?,
        )?;
        Ok(())
    }

    fn next_version(&self, role: Role) -> u32 {
        self.versions[role as usize] + 1
    }

    // wrappers
    // block_on is used here because in reality these are futures that finish after you poll them
    // once
    fn store_metadata<M: Metadata>(&mut self, metadata: M) -> Result<MetadataDescription> {
        let role = Role::from(M::ROLE);
        let path = role.path();
        let metadata: SignedMetadata<Json, _> =
            SignedMetadata::new(&metadata, self.private_key(role))?;
        let metadata_raw = metadata.to_raw()?;
        let version = self.next_version(role);
        self.versions[role as usize] = version;
        futures::executor::block_on(self.repo.store_metadata(
            &path,
            MetadataVersion::None,
            &mut metadata_raw.as_bytes(),
        ))?;
        futures::executor::block_on(self.repo.store_metadata(
            &path,
            MetadataVersion::Number(version),
            &mut metadata_raw.as_bytes(),
        ))?;

        let buf = Json::canonicalize(&Json::serialize(&metadata)?)?;
        Ok(MetadataDescription::from_slice(&buf, version, HASH_ALGS)?)
    }

    fn load_signed_metadata<M: Metadata>(&self) -> Result<SignedMetadata<Json, M>> {
        let path = MetadataPath::from_role(&M::ROLE);
        let mut metadata_reader =
            futures::executor::block_on(self.repo.fetch_metadata(&path, MetadataVersion::None))?;
        let mut buf = Vec::new();
        futures::executor::block_on(metadata_reader.read_to_end(&mut buf))?;
        Ok(Json::from_slice(&buf)?)
    }

    fn load_metadata<M: Metadata>(&self) -> Result<M> {
        // this doesn't check the signature at all, but is there any point in doing so?
        Ok(self.load_signed_metadata::<M>()?.assume_valid()?)
    }
}

#[derive(Copy, Clone)]
enum Role {
    Root,
    Snapshot,
    Targets,
    Timestamp,
}

impl Role {
    fn key_filename(&self) -> &'static str {
        use Role::*;
        match self {
            Root => "root.der",
            Snapshot => "snapshot.der",
            Targets => "targets.der",
            Timestamp => "timestamp.der",
        }
    }
    fn path(self) -> MetadataPath {
        MetadataPath::from_role(&self.into())
    }
}

impl Into<TufRole> for Role {
    fn into(self) -> TufRole {
        match self {
            Role::Root => TufRole::Root,
            Role::Snapshot => TufRole::Snapshot,
            Role::Targets => TufRole::Targets,
            Role::Timestamp => TufRole::Timestamp,
        }
    }
}

impl From<TufRole> for Role {
    fn from(role: TufRole) -> Role {
        match role {
            TufRole::Root => Role::Root,
            TufRole::Snapshot => Role::Snapshot,
            TufRole::Targets => Role::Targets,
            TufRole::Timestamp => Role::Timestamp,
        }
    }
}
