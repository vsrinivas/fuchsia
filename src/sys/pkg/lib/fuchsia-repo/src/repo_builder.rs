// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{repo_client::RepoClient, repo_keys::RepoKeys, repository::RepoStorageProvider},
    anyhow::{anyhow, Context, Result},
    async_fs::File,
    camino::Utf8Path,
    chrono::{DateTime, Duration, Utc},
    fuchsia_pkg::PackageManifest,
    std::collections::HashMap,
    tuf::{
        metadata::TargetPath, pouf::Pouf1, repo_builder::RepoBuilder as TufRepoBuilder, Database,
    },
};

/// Number of days from now before the root metadata is expired.
const DEFAULT_ROOT_EXPIRATION: i64 = 365;

/// Number of days from now before the targets metadata is expired.
const DEFAULT_TARGETS_EXPIRATION: i64 = 90;

/// Number of days from now before the snapshot metadata is expired.
const DEFAULT_SNAPSHOT_EXPIRATION: i64 = 30;

/// Number of days from now before the timestamp metadata is expired.
const DEFAULT_TIMESTAMP_EXPIRATION: i64 = 30;

/// RepoBuilder can create and manipulate package repositories.
pub struct RepoBuilder<'a, R: RepoStorageProvider> {
    repo_keys: &'a RepoKeys,
    database: Option<&'a Database<Pouf1>>,
    repo: R,
    current_time: DateTime<Utc>,
    time_versioning: bool,
    refresh_metadata: bool,
    refresh_non_root_metadata: bool,
    packages: Vec<PackageManifest>,
}

impl<'a, R> RepoBuilder<'a, &'a R>
where
    R: RepoStorageProvider,
{
    pub fn from_client(
        client: &'a RepoClient<R>,
        repo_keys: &'a RepoKeys,
    ) -> RepoBuilder<'a, &'a R> {
        Self::from_database(client.remote_repo(), repo_keys, client.database())
    }
}

impl<'a, R> RepoBuilder<'a, R>
where
    R: RepoStorageProvider,
{
    pub fn create(repo: R, repo_keys: &'a RepoKeys) -> RepoBuilder<'a, R> {
        Self::new(repo, repo_keys, None)
    }

    pub fn from_database(
        repo: R,
        repo_keys: &'a RepoKeys,
        database: &'a Database<Pouf1>,
    ) -> RepoBuilder<'a, R> {
        Self::new(repo, repo_keys, Some(database))
    }

    fn new(
        repo: R,
        repo_keys: &'a RepoKeys,
        database: Option<&'a Database<Pouf1>>,
    ) -> RepoBuilder<'a, R> {
        RepoBuilder {
            repo,
            repo_keys,
            database,
            current_time: Utc::now(),
            time_versioning: false,
            refresh_metadata: false,
            refresh_non_root_metadata: false,
            packages: vec![],
        }
    }

    pub fn current_time(mut self, current_time: DateTime<Utc>) -> Self {
        self.current_time = current_time;
        self
    }

    pub fn time_versioning(mut self, time_versioning: bool) -> Self {
        self.time_versioning = time_versioning;
        self
    }

    /// Always generate new root, targets, snapshot, and timestamp metadata, even if unchanged and
    /// not expired.
    pub fn refresh_metadata(mut self, refresh_metadata: bool) -> Self {
        self.refresh_metadata = refresh_metadata;
        self
    }

    /// Generate a new targets, snapshot, and timestamp metadata, even if unchanged and not expired.
    pub fn refresh_non_root_metadata(mut self, refresh_non_root_metadata: bool) -> Self {
        self.refresh_non_root_metadata = refresh_non_root_metadata;
        self
    }

    pub fn add_package(mut self, package: PackageManifest) -> Self {
        self.packages.push(package);
        self
    }

    pub fn add_packages(mut self, packages: impl Iterator<Item = PackageManifest>) -> Self {
        self.packages.extend(packages);
        self
    }

    pub async fn commit(self) -> Result<()> {
        let repo_builder = if let Some(database) = self.database.as_ref() {
            TufRepoBuilder::from_database(&self.repo, database)
        } else {
            TufRepoBuilder::create(&self.repo)
        };

        // Create a repo builder for the metadata, and initialize it with our repository keys.
        let mut repo_builder = repo_builder
            .current_time(self.current_time)
            .time_versioning(self.time_versioning)
            .root_expiration_duration(Duration::days(DEFAULT_ROOT_EXPIRATION))
            .targets_expiration_duration(Duration::days(DEFAULT_TARGETS_EXPIRATION))
            .snapshot_expiration_duration(Duration::days(DEFAULT_SNAPSHOT_EXPIRATION))
            .timestamp_expiration_duration(Duration::days(DEFAULT_TIMESTAMP_EXPIRATION));

        for key in self.repo_keys.root_keys() {
            repo_builder = repo_builder.trusted_root_keys(&[&**key]);
        }

        for key in self.repo_keys.targets_keys() {
            repo_builder = repo_builder.trusted_targets_keys(&[&**key]);
        }

        for key in self.repo_keys.snapshot_keys() {
            repo_builder = repo_builder.trusted_snapshot_keys(&[&**key]);
        }

        for key in self.repo_keys.timestamp_keys() {
            repo_builder = repo_builder.trusted_timestamp_keys(&[&**key]);
        }

        // We can't generate a new root if we don't have any root keys.
        let mut repo_builder = if self.repo_keys.root_keys().is_empty() {
            repo_builder.skip_root()
        } else if self.refresh_metadata {
            repo_builder.stage_root()?
        } else {
            repo_builder.stage_root_if_necessary()?
        };

        // Gather up all package blobs, and separate out the the meta.far blobs.
        let mut staged_blobs = HashMap::new();
        let mut meta_far_blobs = HashMap::new();

        for package in &self.packages {
            let mut meta_far_blob = None;
            for blob in package.blobs() {
                if blob.path == "meta/" {
                    meta_far_blob = Some(blob.clone());
                }

                staged_blobs.insert(blob.merkle, blob.clone());
            }

            let meta_far_blob = meta_far_blob
                .ok_or_else(|| anyhow!("package does not contain entry for meta.far"))?;

            meta_far_blobs
                .insert(meta_far_blob.merkle, (package.package_path().to_string(), meta_far_blob));
        }

        // Make sure all the blobs exist.
        for blob in staged_blobs.values() {
            async_fs::metadata(&blob.source_path)
                .await
                .with_context(|| format!("reading {}", blob.source_path))?;
        }

        // Stage the metadata blobs.
        for (package_path, meta_far_blob) in meta_far_blobs.values() {
            let target_path = TargetPath::new(package_path)?;
            let mut custom = HashMap::new();

            custom.insert("merkle".into(), serde_json::to_value(meta_far_blob.merkle)?);
            custom.insert("size".into(), serde_json::to_value(meta_far_blob.size)?);

            let f = File::open(&meta_far_blob.source_path).await?;

            repo_builder = repo_builder.add_target_with_custom(target_path, f, custom).await?;
        }

        // Stage the targets metadata. If we're forcing a metadata refresh, force a new targets,
        // snapshot, and timestamp, even if nothing changed in the contents.
        if self.refresh_metadata || self.refresh_non_root_metadata {
            repo_builder.stage_targets()?.stage_snapshot()?.stage_timestamp()?.commit().await
        } else {
            repo_builder.commit().await
        }
        .context("publishing metadata")?;

        // Stage the blobs.
        for (blob_hash, blob) in staged_blobs {
            self.repo.store_blob(&blob_hash, Utf8Path::new(&blob.source_path)).await?;
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            repo_client::RepoClient,
            repository::{FileSystemRepository, PmRepository},
            test_utils,
        },
        assert_matches::assert_matches,
        camino::Utf8Path,
        pretty_assertions::{assert_eq, assert_ne},
        std::collections::{BTreeMap, HashMap},
        tuf::metadata::{Metadata as _, MetadataPath},
        walkdir::WalkDir,
    };

    pub(crate) fn read_dir(dir: &Utf8Path) -> BTreeMap<String, Vec<u8>> {
        let mut entries = BTreeMap::new();
        for entry in WalkDir::new(dir) {
            let entry = entry.unwrap();
            if entry.metadata().unwrap().is_file() {
                let path = entry.path().strip_prefix(&dir).unwrap().to_str().unwrap().to_string();
                let contents = std::fs::read(entry.path()).unwrap();

                entries.insert(path, contents);
            }
        }

        entries
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_create() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        let repo = PmRepository::new(dir.to_path_buf());
        let repo_keys = test_utils::make_repo_keys();

        RepoBuilder::create(&repo, &repo_keys).commit().await.unwrap();

        // Make sure we can update a client from this metadata.
        let mut repo_client = RepoClient::from_trusted_remote(repo).await.unwrap();
        assert_matches!(repo_client.update().await, Ok(true));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_create_and_update_repo() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        let metadata_repo_path = dir.join("metadata");
        let blob_repo_path = dir.join("blobs");
        let repo = FileSystemRepository::new(metadata_repo_path, blob_repo_path.clone());
        let repo_keys = test_utils::make_repo_keys();

        let pkg1_dir = dir.join("package1");
        let (pkg1_meta_far_path, pkg1_manifest) =
            test_utils::make_package_manifest("package1", pkg1_dir.as_std_path());
        let pkg1_meta_far_contents = std::fs::read(&pkg1_meta_far_path).unwrap();

        RepoBuilder::create(&repo, &repo_keys).add_package(pkg1_manifest).commit().await.unwrap();

        // Make sure we wrote all the blobs from package1.
        assert_eq!(
            read_dir(&blob_repo_path),
            BTreeMap::from([
                (test_utils::PKG1_HASH.into(), pkg1_meta_far_contents.clone()),
                (test_utils::PKG1_BIN_HASH.into(), b"binary package1".to_vec()),
                (test_utils::PKG1_LIB_HASH.into(), b"lib package1".to_vec()),
            ])
        );

        // Make sure we can update a client from this metadata.
        let mut repo_client = RepoClient::from_trusted_remote(repo).await.unwrap();
        assert_matches!(repo_client.update().await, Ok(true));

        assert_eq!(repo_client.database().trusted_root().version(), 1);
        assert_eq!(repo_client.database().trusted_targets().map(|m| m.version()), Some(1));
        assert_eq!(repo_client.database().trusted_snapshot().map(|m| m.version()), Some(1));
        assert_eq!(repo_client.database().trusted_timestamp().map(|m| m.version()), Some(1));

        // Create the next version of the metadata and add a new package to it.
        let pkg2_dir = dir.join("package2");
        let (pkg2_meta_far_path, pkg2_manifest) =
            test_utils::make_package_manifest("package2", pkg2_dir.as_std_path());
        let pkg2_meta_far_contents = std::fs::read(&pkg2_meta_far_path).unwrap();

        RepoBuilder::from_client(&repo_client, &repo_keys)
            .add_package(pkg2_manifest)
            .commit()
            .await
            .unwrap();

        // Make sure we wrote all the blobs from package1 and package2.
        assert_eq!(
            read_dir(&blob_repo_path),
            BTreeMap::from([
                (test_utils::PKG1_HASH.into(), pkg1_meta_far_contents.clone()),
                (test_utils::PKG1_BIN_HASH.into(), b"binary package1".to_vec()),
                (test_utils::PKG1_LIB_HASH.into(), b"lib package1".to_vec()),
                (test_utils::PKG2_HASH.into(), pkg2_meta_far_contents.clone()),
                (test_utils::PKG2_BIN_HASH.into(), b"binary package2".to_vec()),
                (test_utils::PKG2_LIB_HASH.into(), b"lib package2".to_vec()),
            ])
        );

        // Make sure we can resolve the new metadata.
        assert_matches!(repo_client.update().await, Ok(true));
        assert_eq!(repo_client.database().trusted_root().version(), 1);
        assert_eq!(repo_client.database().trusted_targets().map(|m| m.version()), Some(2));
        assert_eq!(repo_client.database().trusted_snapshot().map(|m| m.version()), Some(2));
        assert_eq!(repo_client.database().trusted_timestamp().map(|m| m.version()), Some(2));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_refresh_metadata_with_all_keys() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        // Load up the test metadata, which was created some time ago, and has a different
        // expiration date.
        let repo = test_utils::make_pm_repository(dir).await;

        // Download the older metadata before we refresh it.
        let mut repo_client = RepoClient::from_trusted_remote(&repo).await.unwrap();
        repo_client.update().await.unwrap();

        let root1 = (*repo_client.database().trusted_root()).clone();
        let targets1 = repo_client.database().trusted_targets().cloned().unwrap();
        let snapshot1 = repo_client.database().trusted_snapshot().cloned().unwrap();
        let timestamp1 = repo_client.database().trusted_timestamp().cloned().unwrap();

        // Update the metadata expiration.
        let repo_keys = RepoKeys::from_dir(&dir.join("keys").into_std_path_buf()).unwrap();
        RepoBuilder::from_database(repo_client.remote_repo(), &repo_keys, repo_client.database())
            .refresh_metadata(true)
            .commit()
            .await
            .unwrap();

        // Finally, make sure the metadata has changed.
        assert_matches!(repo_client.update().await, Ok(true));

        let root2 = (*repo_client.database().trusted_root()).clone();
        let targets2 = repo_client.database().trusted_targets().cloned().unwrap();
        let snapshot2 = repo_client.database().trusted_snapshot().cloned().unwrap();
        let timestamp2 = repo_client.database().trusted_timestamp().cloned().unwrap();

        // Make sure we generated new metadata.
        assert_ne!(root1, root2);
        assert_ne!(targets1, targets2);
        assert_ne!(snapshot1, snapshot2);
        assert_ne!(timestamp1, timestamp2);

        // We should have kept our old snapshot entries (except the target should have changed).
        assert_eq!(
            snapshot1
                .meta()
                .iter()
                .filter(|(k, _)| **k != MetadataPath::targets())
                .collect::<HashMap<_, _>>(),
            snapshot2
                .meta()
                .iter()
                .filter(|(k, _)| **k != MetadataPath::targets())
                .collect::<HashMap<_, _>>(),
        );

        // We should have kept our targets and delegations.
        assert_eq!(targets1.targets(), targets2.targets());
        assert_eq!(targets1.delegations(), targets2.delegations());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_refresh_metadata_with_some_keys() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        // Load the repo.
        let repo = test_utils::make_pm_repository(dir).await;

        // Download the older metadata before we refresh it.
        let mut repo_client = RepoClient::from_trusted_remote(&repo).await.unwrap();
        repo_client.update().await.unwrap();

        let root1 = (*repo_client.database().trusted_root()).clone();
        let targets1 = repo_client.database().trusted_targets().cloned().unwrap();
        let snapshot1 = repo_client.database().trusted_snapshot().cloned().unwrap();
        let timestamp1 = repo_client.database().trusted_timestamp().cloned().unwrap();

        // Load the repo, but delete the root private key file.
        let keys_dir = dir.join("keys");
        std::fs::remove_file(keys_dir.join("root.json")).unwrap();

        // Update the metadata expiration.
        let repo_keys = RepoKeys::from_dir(&dir.join("keys").into_std_path_buf()).unwrap();

        // Update the metadata expiration should succeed.
        RepoBuilder::from_database(repo_client.remote_repo(), &repo_keys, repo_client.database())
            .refresh_metadata(true)
            .commit()
            .await
            .unwrap();

        // Make sure the metadata has changed.
        assert_matches!(repo_client.update().await, Ok(true));

        let root2 = (*repo_client.database().trusted_root()).clone();
        let targets2 = repo_client.database().trusted_targets().cloned().unwrap();
        let snapshot2 = repo_client.database().trusted_snapshot().cloned().unwrap();
        let timestamp2 = repo_client.database().trusted_timestamp().cloned().unwrap();

        // Make sure we generated new metadata, except for the root metadata.
        assert_eq!(root1, root2);
        assert_ne!(targets1, targets2);
        assert_ne!(snapshot1, snapshot2);
        assert_ne!(timestamp1, timestamp2);

        // We should have kept our old snapshot entries (except the target should have changed).
        assert_eq!(
            snapshot1
                .meta()
                .iter()
                .filter(|(k, _)| **k != MetadataPath::targets())
                .collect::<HashMap<_, _>>(),
            snapshot2
                .meta()
                .iter()
                .filter(|(k, _)| **k != MetadataPath::targets())
                .collect::<HashMap<_, _>>(),
        );

        // We should have kept our targets and delegations.
        assert_eq!(targets1.targets(), targets2.targets());
        assert_eq!(targets1.delegations(), targets2.delegations());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_refresh_metadata_with_no_keys() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        // Load the repo.
        let repo = test_utils::make_pm_repository(dir).await;

        // Download the older metadata before we refresh it.
        let mut repo_client = RepoClient::from_trusted_remote(&repo).await.unwrap();
        repo_client.update().await.unwrap();

        let root1 = (*repo_client.database().trusted_root()).clone();
        let targets1 = repo_client.database().trusted_targets().cloned().unwrap();
        let snapshot1 = repo_client.database().trusted_snapshot().cloned().unwrap();
        let timestamp1 = repo_client.database().trusted_timestamp().cloned().unwrap();

        // Try to refresh the metadata with an empty key set, which should error out.
        let repo_keys = RepoKeys::builder().build();
        let res = RepoBuilder::from_database(
            repo_client.remote_repo(),
            &repo_keys,
            repo_client.database(),
        )
        .refresh_metadata(true)
        .commit()
        .await;
        assert_matches!(res, Err(_));

        // Updating the client should return that there were no changes.
        assert_matches!(repo_client.update().await, Ok(false));

        let root2 = (*repo_client.database().trusted_root()).clone();
        let targets2 = repo_client.database().trusted_targets().cloned().unwrap();
        let snapshot2 = repo_client.database().trusted_snapshot().cloned().unwrap();
        let timestamp2 = repo_client.database().trusted_timestamp().cloned().unwrap();

        // We should not have changed the metadata.
        assert_eq!(root1, root2);
        assert_eq!(targets1, targets2);
        assert_eq!(snapshot1, snapshot2);
        assert_eq!(timestamp1, timestamp2);
    }
}
