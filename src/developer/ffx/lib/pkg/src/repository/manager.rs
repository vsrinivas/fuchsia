// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::Repository,
    parking_lot::RwLock,
    std::{collections::HashMap, sync::Arc},
};

type ArcRepository = Arc<dyn Repository + Send + Sync>;

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
