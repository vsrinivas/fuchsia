// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        Error, FileSystemRepository, RepositoryBackend, RepositorySpec, Resource, ResourceRange,
    },
    anyhow::Result,
    camino::Utf8PathBuf,
    futures::stream::BoxStream,
    std::time::SystemTime,
    tuf::{interchange::Json, repository::RepositoryProvider},
};

/// Serve a repository from a local pm repository.
#[derive(Debug)]
pub struct PmRepository {
    pm_repo_path: Utf8PathBuf,
    repo: FileSystemRepository,
}

impl PmRepository {
    /// Construct a [PmRepository].
    pub fn new(pm_repo_path: Utf8PathBuf) -> Self {
        let metadata_repo_path = pm_repo_path.join("repository");
        let blob_repo_path = metadata_repo_path.join("blobs");
        let repo = FileSystemRepository::new(metadata_repo_path, blob_repo_path);
        Self { pm_repo_path, repo }
    }
}

#[async_trait::async_trait]
impl RepositoryBackend for PmRepository {
    fn spec(&self) -> RepositorySpec {
        RepositorySpec::Pm { path: self.pm_repo_path.clone() }
    }

    async fn fetch_metadata(
        &self,
        resource_path: &str,
        range: ResourceRange,
    ) -> Result<Resource, Error> {
        self.repo.fetch_metadata(resource_path, range).await
    }

    async fn fetch_blob(
        &self,
        resource_path: &str,
        range: ResourceRange,
    ) -> Result<Resource, Error> {
        self.repo.fetch_blob(resource_path, range).await
    }

    fn supports_watch(&self) -> bool {
        self.repo.supports_watch()
    }

    fn watch(&self) -> Result<BoxStream<'static, ()>> {
        self.repo.watch()
    }

    fn get_tuf_repo(&self) -> Result<Box<(dyn RepositoryProvider<Json> + 'static)>, Error> {
        self.repo.get_tuf_repo()
    }

    async fn blob_modification_time(&self, path: &str) -> Result<Option<SystemTime>> {
        self.repo.blob_modification_time(path).await
    }
}
