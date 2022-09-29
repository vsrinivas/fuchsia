// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        range::Range,
        repository::{Error, FileSystemRepository, RepoProvider, RepoStorage, RepositorySpec},
        resource::Resource,
    },
    anyhow::Result,
    camino::{Utf8Path, Utf8PathBuf},
    fuchsia_merkle::Hash,
    futures::{future::BoxFuture, stream::BoxStream, AsyncRead},
    std::{fmt::Debug, time::SystemTime},
    tuf::{
        metadata::{MetadataPath, MetadataVersion, TargetPath},
        pouf::Pouf1,
        repository::{
            RepositoryProvider as TufRepositoryProvider, RepositoryStorage as TufRepositoryStorage,
        },
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
    pub fn new(pm_repo_path: Utf8PathBuf) -> Self {
        let metadata_repo_path = pm_repo_path.join("repository");
        let blob_repo_path = metadata_repo_path.join("blobs");
        let repo = FileSystemRepository::new(metadata_repo_path, blob_repo_path);

        Self { pm_repo_path, repo }
    }
}

impl RepoProvider for PmRepository {
    fn spec(&self) -> RepositorySpec {
        RepositorySpec::Pm { path: self.pm_repo_path.clone() }
    }

    fn fetch_metadata_range<'a>(
        &'a self,
        resource_path: &str,
        range: Range,
    ) -> BoxFuture<'a, Result<Resource, Error>> {
        self.repo.fetch_metadata_range(resource_path, range)
    }

    fn fetch_blob_range<'a>(
        &'a self,
        resource_path: &str,
        range: Range,
    ) -> BoxFuture<'a, Result<Resource, Error>> {
        self.repo.fetch_blob_range(resource_path, range)
    }

    fn supports_watch(&self) -> bool {
        self.repo.supports_watch()
    }

    fn watch(&self) -> Result<BoxStream<'static, ()>> {
        self.repo.watch()
    }

    fn blob_len<'a>(&'a self, path: &str) -> BoxFuture<'a, Result<u64>> {
        self.repo.blob_len(path)
    }

    fn blob_modification_time<'a>(
        &'a self,
        path: &str,
    ) -> BoxFuture<'a, Result<Option<SystemTime>>> {
        self.repo.blob_modification_time(path)
    }
}

impl TufRepositoryProvider<Pouf1> for PmRepository {
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

impl TufRepositoryStorage<Pouf1> for PmRepository {
    fn store_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
        metadata: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, tuf::Result<()>> {
        self.repo.store_metadata(meta_path, version, metadata)
    }

    fn store_target<'a>(
        &'a self,
        target_path: &TargetPath,
        target: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, tuf::Result<()>> {
        self.repo.store_target(target_path, target)
    }
}

impl RepoStorage for PmRepository {
    fn store_blob<'a>(&'a self, hash: &Hash, path: &Utf8Path) -> BoxFuture<'a, Result<()>> {
        self.repo.store_blob(hash, path)
    }
}
