// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        range::Range,
        repo_storage::RepoStorage,
        repository::{Error, FileSystemRepository, RepoProvider, RepositorySpec},
        resource::Resource,
    },
    anyhow::Result,
    camino::Utf8PathBuf,
    futures::{future::BoxFuture, stream::BoxStream, AsyncRead},
    std::{fmt::Debug, time::SystemTime},
    tuf::{
        interchange::Json,
        metadata::{MetadataPath, MetadataVersion, TargetPath},
        repository::{RepositoryProvider as TufRepositoryProvider, RepositoryStorageProvider},
    },
};

/// Serve a repository from a local pm repository.
#[derive(Debug)]
pub struct PmRepository {
    pm_repo_path: Utf8PathBuf,
    repo: FileSystemRepository,
}

impl PmRepository {
    /// Construct a [PmRepository].
    pub fn new(pm_repo_path: Utf8PathBuf) -> Result<Self> {
        let metadata_repo_path = pm_repo_path.join("repository");
        let blob_repo_path = metadata_repo_path.join("blobs");
        let repo = FileSystemRepository::new(metadata_repo_path, blob_repo_path)?;

        Ok(Self { pm_repo_path, repo })
    }
}

#[async_trait::async_trait]
impl RepoProvider for PmRepository {
    fn spec(&self) -> RepositorySpec {
        RepositorySpec::Pm { path: self.pm_repo_path.clone() }
    }

    async fn fetch_metadata_range(
        &self,
        resource_path: &str,
        range: Range,
    ) -> Result<Resource, Error> {
        self.repo.fetch_metadata_range(resource_path, range).await
    }

    async fn fetch_blob_range(&self, resource_path: &str, range: Range) -> Result<Resource, Error> {
        self.repo.fetch_blob_range(resource_path, range).await
    }

    fn supports_watch(&self) -> bool {
        self.repo.supports_watch()
    }

    fn watch(&self) -> Result<BoxStream<'static, ()>> {
        self.repo.watch()
    }

    async fn blob_len(&self, path: &str) -> Result<u64> {
        self.repo.blob_len(path).await
    }

    async fn blob_modification_time(&self, path: &str) -> Result<Option<SystemTime>> {
        self.repo.blob_modification_time(path).await
    }
}

impl TufRepositoryProvider<Json> for PmRepository {
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
    ) -> BoxFuture<'a, tuf::Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        self.repo.fetch_metadata(meta_path, version)
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &TargetPath,
    ) -> BoxFuture<'a, tuf::Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        self.repo.fetch_target(target_path)
    }
}

impl RepoStorage for PmRepository {
    fn get_tuf_repo_storage(
        &self,
    ) -> Result<Box<dyn RepositoryStorageProvider<Json> + Send + Sync>> {
        self.repo.get_tuf_repo_storage()
    }
}
