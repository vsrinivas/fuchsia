// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{repo_client::RepoClient, repository::RepoProvider},
    async_lock::RwLock as AsyncRwLock,
    std::{
        collections::HashMap,
        sync::{Arc, RwLock as SyncRwLock},
    },
};

type ArcRepoClient = Arc<AsyncRwLock<RepoClient<Box<dyn RepoProvider>>>>;

/// RepositoryManager is responsible for managing all the repositories in use by ffx.
pub struct RepositoryManager {
    repositories: SyncRwLock<HashMap<String, ArcRepoClient>>,
}

impl RepositoryManager {
    /// Construct a new [RepositoryManager].
    pub fn new() -> Arc<Self> {
        Arc::new(Self { repositories: SyncRwLock::new(HashMap::new()) })
    }

    /// Add a [Repository] to the [RepositoryManager].
    pub fn add(&self, repo_name: impl Into<String>, repo: RepoClient<Box<dyn RepoProvider>>) {
        let repo_name = repo_name.into();
        self.repositories.write().unwrap().insert(repo_name, Arc::new(AsyncRwLock::new(repo)));
    }

    /// Get a [Repository].
    pub fn get(&self, repo_name: &str) -> Option<ArcRepoClient> {
        self.repositories.read().unwrap().get(repo_name).map(|repo| Arc::clone(repo))
    }

    /// Remove a [Repository] from the [RepositoryManager].
    pub fn remove(&self, name: &str) -> bool {
        self.repositories.write().unwrap().remove(name).is_some()
    }

    /// Removes all [Repositories](Repository) from the [RepositoryManager].
    pub fn clear(&self) {
        self.repositories.write().unwrap().clear();
    }

    /// Iterate through all [Repositories].
    pub fn repositories<'a>(
        &'a self,
    ) -> impl std::iter::Iterator<Item = (String, ArcRepoClient)> + 'a {
        let mut repositories = self
            .repositories
            .read()
            .unwrap()
            .iter()
            .map(|(name, repo)| (name.to_owned(), Arc::clone(repo)))
            .collect::<Vec<_>>();

        // Sort the repositories so it's in a stable order.
        repositories.sort_unstable_by(|(lhs, _), (rhs, _)| lhs.cmp(&rhs));

        repositories.into_iter()
    }
}

#[cfg(test)]
mod test {
    use {super::*, crate::test_utils::make_readonly_empty_repository};
    const REPO_NAME: &str = "fake-repo";

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add() {
        let repo = make_readonly_empty_repository().await.unwrap();

        let manager = RepositoryManager::new();
        manager.add(REPO_NAME, repo);

        assert_eq!(
            manager.repositories().map(|(name, _)| name).collect::<Vec<_>>(),
            vec![REPO_NAME.to_owned()]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_accepts_valid_names() {
        for name in ["fuchsia.com", "my-repository", "hello.there.world"] {
            let repo = make_readonly_empty_repository().await.unwrap();

            let manager = RepositoryManager::new();
            manager.add(name, repo);
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_remove() {
        let repo = make_readonly_empty_repository().await.unwrap();

        let manager = RepositoryManager::new();
        manager.add(REPO_NAME, repo);

        assert_eq!(
            manager.repositories().map(|(name, _)| name).collect::<Vec<_>>(),
            vec![REPO_NAME.to_owned()]
        );

        manager.remove(REPO_NAME);
        assert!(manager.repositories().next().is_none());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_clear() {
        let repo1 = make_readonly_empty_repository().await.unwrap();
        let repo2 = make_readonly_empty_repository().await.unwrap();

        let manager = RepositoryManager::new();
        manager.add("repo1", repo1);
        manager.add("repo2", repo2);

        manager.clear();

        assert!(manager.repositories().next().is_none());
    }
}
