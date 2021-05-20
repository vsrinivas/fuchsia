// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{file_system::FileSystemRepository, Error, Repository, RepositoryBackend},
    anyhow::Result,
    parking_lot::RwLock,
    serde::{Deserialize, Serialize},
    serde_json::Value,
    std::{collections::HashMap, sync::Arc},
};

#[cfg(test)]
use super::RepositoryMetadata;

type ArcRepository = Arc<Repository>;

#[derive(Serialize, Deserialize)]
#[serde(tag = "type")]
pub enum RepositorySpec {
    #[serde(rename = "fs")]
    FileSystem { path: std::path::PathBuf },
}

/// RepositoryManager is responsible for managing all the repositories in use by ffx.
pub struct RepositoryManager {
    repositories: RwLock<HashMap<String, ArcRepository>>,
}

impl RepositoryManager {
    /// Construct a new [RepositoryManager].
    pub fn new() -> Arc<Self> {
        Arc::new(Self { repositories: RwLock::new(HashMap::new()) })
    }

    /// Add a [Repository] to the [RepositoryManager].
    pub fn add(&self, repo: ArcRepository) {
        self.repositories.write().insert(repo.name().to_string(), repo);
    }

    fn get_backend(&self, spec: Value) -> Result<Box<dyn RepositoryBackend + Send + Sync>, Error> {
        let spec = serde_json::from_value(spec).map_err(|e| anyhow::anyhow!(e))?;
        match spec {
            RepositorySpec::FileSystem { path } => Ok(Box::new(FileSystemRepository::new(path))),
        }
    }

    /// Use a configuration value to instantiate a [Repository] and add it to the [RepositoryManager].
    pub async fn add_from_config(&self, name: String, spec: Value) -> Result<(), Error> {
        let repo = match () {
            #[cfg(test)]
            () => Repository::new_with_metadata(
                &name,
                self.get_backend(spec)?,
                RepositoryMetadata::default(),
            ),
            #[cfg(not(test))]
            () => Repository::new(&name, self.get_backend(spec)?).await?,
        };
        self.add(Arc::new(repo));
        Ok(())
    }

    #[cfg(test)]
    async fn add_from_config_with_metadata(
        &self,
        name: String,
        spec: Value,
        metadata: RepositoryMetadata,
    ) -> Result<(), Error> {
        self.add(Arc::new(Repository::new_with_metadata(&name, self.get_backend(spec)?, metadata)));
        Ok(())
    }

    /// Get a [Repository].
    pub fn get(&self, repo_name: &str) -> Option<ArcRepository> {
        self.repositories.read().get(repo_name).map(|repo| Arc::clone(repo))
    }

    /// Remove a [Repository] from the [RepositoryManager].
    pub fn remove(&self, name: &str) -> bool {
        self.repositories.write().remove(name).is_some()
    }

    /// Iterate through all [Repositories](Repository)
    pub fn repositories<'a>(&'a self) -> impl std::iter::Iterator<Item = ArcRepository> + 'a {
        let mut ret = self.repositories.read().values().map(Arc::clone).collect::<Vec<_>>();
        ret.sort_unstable_by_key(|x| x.name().to_owned());
        ret.into_iter()
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use {fuchsia_async as fasync, serde_json::json};

    #[fasync::run_singlethreaded(test)]
    async fn fs_backend_from_config() {
        let value = json!({ "type": "fs", "path": "/nowhere" });
        let manager = RepositoryManager::new();
        manager
            .add_from_config_with_metadata(
                "my_repo".to_owned(),
                value,
                RepositoryMetadata::default(),
            )
            .await
            .expect("Error adding value");

        assert_eq!(
            manager.repositories().map(|x| x.name().to_owned()).collect::<Vec<_>>(),
            vec!["my_repo".to_owned()]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn remove_test() {
        let value = json!({ "type": "fs", "path": "/nowhere" });
        let manager = RepositoryManager::new();
        manager.add_from_config("my_repo".to_owned(), value).await.expect("Error adding value");

        assert_eq!(
            manager.repositories().map(|x| x.name().to_owned()).collect::<Vec<_>>(),
            vec!["my_repo".to_owned()]
        );

        manager.remove("my_repo");
        assert!(manager.repositories().next().is_none());
    }
}
