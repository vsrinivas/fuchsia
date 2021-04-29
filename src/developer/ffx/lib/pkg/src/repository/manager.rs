// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{file_system::FileSystemRepository, Repository},
    anyhow::Result,
    parking_lot::RwLock,
    serde::{Deserialize, Serialize},
    serde_json::Value,
    std::{collections::HashMap, sync::Arc},
};

type ArcRepository = Arc<dyn Repository + Send + Sync>;

#[derive(Serialize, Deserialize)]
#[serde(tag = "type")]
enum RepositorySpec {
    #[serde(rename = "fs")]
    FileSystem { path: std::path::PathBuf },
}

/// RepositoryManager is responsible for managing all the repositories in use by ffx.
pub struct RepositoryManager {
    repositories: Arc<RwLock<HashMap<String, ArcRepository>>>,
}

impl RepositoryManager {
    /// Construct a new [RepositoryManager].
    pub fn new() -> Arc<Self> {
        Arc::new(Self { repositories: Arc::new(RwLock::new(HashMap::new())) })
    }

    /// Add a [Repository] to the [RepositoryManager].
    pub fn add(&self, repo: ArcRepository) {
        self.repositories.write().insert(repo.name().to_string(), repo);
    }

    /// Use a configuration value to instantiate a [Repository] and add it to the [RepositoryManager].
    pub fn add_from_config(&self, name: String, spec: Value) -> Result<()> {
        let spec = serde_json::from_value(spec)?;
        match spec {
            RepositorySpec::FileSystem { path } => {
                self.add(Arc::new(FileSystemRepository::new(name, path)));
                Ok(())
            }
        }
    }

    /// Get a [Repository].
    pub fn get(&self, repo_name: &str) -> Option<ArcRepository> {
        self.repositories.read().get(repo_name).map(|repo| Arc::clone(repo))
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
    async fn fs_from_config() {
        let value = json!({ "type": "fs", "path": "/nowhere" });
        let manager = RepositoryManager::new();
        manager.add_from_config("my_repo".to_owned(), value).expect("Error adding value");

        assert_eq!(
            manager.repositories().map(|x| x.name().to_owned()).collect::<Vec<_>>(),
            vec!["my_repo".to_owned()]
        );
    }
}
