// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::Repository,
    parking_lot::RwLock,
    std::{collections::HashMap, sync::Arc},
};

#[cfg(test)]
use super::RepositoryMetadata;

type ArcRepository = Arc<Repository>;

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

    /// Get a [Repository].
    pub fn get(&self, repo_name: &str) -> Option<ArcRepository> {
        self.repositories.read().get(repo_name).map(|repo| Arc::clone(repo))
    }

    /// Remove a [Repository] from the [RepositoryManager].
    pub fn remove(&self, name: &str) -> bool {
        self.repositories.write().remove(name).is_some()
    }

    /// Iterate through all [Repositories].
    pub fn repositories<'a>(&'a self) -> impl std::iter::Iterator<Item = ArcRepository> + 'a {
        let mut ret = self.repositories.read().values().map(Arc::clone).collect::<Vec<_>>();
        ret.sort_unstable_by_key(|x| x.name().to_owned());
        ret.into_iter()
    }
}

#[cfg(test)]
mod test {
    use {super::*, crate::repository::FileSystemRepository};

    #[test]
    fn test_add() {
        let repo = Repository::new_with_metadata(
            "my_repo",
            Box::new(FileSystemRepository::new("/nowhere".into())),
            RepositoryMetadata::default(),
        );

        let manager = RepositoryManager::new();
        manager.add(Arc::new(repo));

        assert_eq!(
            manager.repositories().map(|x| x.name().to_owned()).collect::<Vec<_>>(),
            vec!["my_repo".to_owned()]
        );
    }

    #[test]
    fn test_remove() {
        let repo = Repository::new_with_metadata(
            "my_repo",
            Box::new(FileSystemRepository::new("/nowhere".into())),
            RepositoryMetadata::default(),
        );

        let manager = RepositoryManager::new();
        manager.add(Arc::new(repo));

        assert_eq!(
            manager.repositories().map(|x| x.name().to_owned()).collect::<Vec<_>>(),
            vec!["my_repo".to_owned()]
        );

        manager.remove("my_repo");
        assert!(manager.repositories().next().is_none());
    }
}
