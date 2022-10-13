// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::repository_manager::{InsertError, RemoveError, RepositoryManager};
use anyhow::anyhow;
use async_lock::RwLock;
use fidl_fuchsia_pkg::{
    MirrorConfig as FidlMirrorConfig, RepositoryConfig as FidlRepositoryConfig,
    RepositoryIteratorRequest, RepositoryIteratorRequestStream, RepositoryManagerRequest,
    RepositoryManagerRequestStream,
};
use fidl_fuchsia_pkg_ext::RepositoryConfig;
use fuchsia_async as fasync;
use fuchsia_url::RepositoryUrl;
use fuchsia_zircon::Status;
use futures::prelude::*;
use std::convert::TryFrom;
use std::sync::Arc;
use tracing::{error, info};

const LIST_CHUNK_SIZE: usize = 100;

pub struct RepositoryService {
    repo_manager: Arc<RwLock<RepositoryManager>>,
}

impl RepositoryService {
    pub fn new(repo_manager: Arc<RwLock<RepositoryManager>>) -> Self {
        RepositoryService { repo_manager }
    }

    pub async fn run(
        &mut self,
        mut stream: RepositoryManagerRequestStream,
    ) -> Result<(), anyhow::Error> {
        while let Some(event) = stream.try_next().await? {
            match event {
                RepositoryManagerRequest::Add { repo, responder } => {
                    let mut response = self.serve_insert(repo).map_err(|s| s.into_raw()).await;
                    responder.send(&mut response)?;
                }
                RepositoryManagerRequest::Remove { repo_url, responder } => {
                    let mut response = self.serve_remove(repo_url).map_err(|s| s.into_raw()).await;
                    responder.send(&mut response)?;
                }
                RepositoryManagerRequest::AddMirror { repo_url, mirror, responder } => {
                    let mut response =
                        self.serve_insert_mirror(repo_url, mirror).map_err(|s| s.into_raw());
                    responder.send(&mut response)?;
                }
                RepositoryManagerRequest::RemoveMirror { repo_url, mirror_url, responder } => {
                    let mut response =
                        self.serve_remove_mirror(repo_url, mirror_url).map_err(|s| s.into_raw());
                    responder.send(&mut response)?;
                }
                RepositoryManagerRequest::List { iterator, control_handle: _ } => {
                    let stream = iterator.into_stream()?;
                    self.serve_list(stream).await;
                }
            }
        }

        Ok(())
    }

    async fn serve_insert(&mut self, repo: FidlRepositoryConfig) -> Result<(), Status> {
        info!("inserting repository {:?}", repo.repo_url);

        let repo = match RepositoryConfig::try_from(repo) {
            Ok(repo) => repo,
            Err(err) => {
                error!("invalid repository config: {:#}", anyhow!(err));
                return Err(Status::INVALID_ARGS);
            }
        };

        match self.repo_manager.write().await.insert(repo).await {
            Ok(_) => Ok(()),
            Err(err @ InsertError::DynamicConfigurationDisabled) => {
                error!("error inserting repository: {:#}", anyhow!(err));
                Err(Status::ACCESS_DENIED)
            }
        }
    }

    async fn serve_remove(&mut self, repo_url: String) -> Result<(), Status> {
        info!("removing repository {}", repo_url);

        let repo_url = match RepositoryUrl::parse(&repo_url) {
            Ok(repo_url) => repo_url,
            Err(err) => {
                error!("invalid repository URL: {:#}", anyhow!(err));
                return Err(Status::INVALID_ARGS);
            }
        };

        match self.repo_manager.write().await.remove(&repo_url).await {
            Ok(Some(_)) => Ok(()),
            Ok(None) => Err(Status::NOT_FOUND),
            Err(e) => match e {
                RemoveError::CannotRemoveStaticRepositories
                | RemoveError::DynamicConfigurationDisabled => Err(Status::ACCESS_DENIED),
            },
        }
    }

    fn serve_insert_mirror(
        &mut self,
        _repo_url: String,
        _mirror: FidlMirrorConfig,
    ) -> Result<(), Status> {
        Err(Status::INTERNAL)
    }

    fn serve_remove_mirror(
        &mut self,
        _repo_url: String,
        _mirror_url: String,
    ) -> Result<(), Status> {
        Err(Status::INTERNAL)
    }

    async fn serve_list(&self, mut stream: RepositoryIteratorRequestStream) {
        let results = self
            .repo_manager
            .read()
            .await
            .list()
            .map(|config| config.clone().into())
            .collect::<Vec<FidlRepositoryConfig>>();

        fasync::Task::spawn(
            async move {
                let mut iter = results.into_iter();

                while let Some(RepositoryIteratorRequest::Next { responder }) =
                    stream.try_next().await?
                {
                    responder.send(&mut iter.by_ref().take(LIST_CHUNK_SIZE))?;
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| error!("error running list protocol: {:#}", e)),
        )
        .detach();
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::repository_manager::RepositoryManagerBuilder,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_pkg::RepositoryIteratorMarker,
        fidl_fuchsia_pkg_ext::{RepositoryConfig, RepositoryConfigBuilder},
        std::convert::TryInto,
    };

    async fn list(service: &RepositoryService) -> Vec<RepositoryConfig> {
        let (list_iterator, stream) =
            create_proxy_and_stream::<RepositoryIteratorMarker>().unwrap();
        service.serve_list(stream).await;

        let mut results: Vec<RepositoryConfig> = Vec::new();
        loop {
            let chunk = list_iterator.next().await.unwrap();
            if chunk.len() == 0 {
                break;
            }
            assert!(chunk.len() <= LIST_CHUNK_SIZE);

            results.extend(chunk.into_iter().map(|config| config.try_into().unwrap()));
        }

        results
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_empty() {
        let dynamic_dir = tempfile::tempdir().unwrap();
        let mgr =
            RepositoryManagerBuilder::new_test(&dynamic_dir, Some("config")).await.unwrap().build();
        let service = RepositoryService::new(Arc::new(RwLock::new(mgr)));

        let results = list(&service).await;
        assert_eq!(results, vec![]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list() {
        // First, create a bunch of repo configs we're going to use for testing.
        let configs = (0..200)
            .map(|i| {
                let url =
                    RepositoryUrl::parse(&format!("fuchsia-pkg://fuchsia{:04}.com", i)).unwrap();
                RepositoryConfigBuilder::new(url).build()
            })
            .collect::<Vec<_>>();

        let dynamic_dir = tempfile::tempdir().unwrap();
        let mgr = RepositoryManagerBuilder::new_test(&dynamic_dir, Some("config"))
            .await
            .unwrap()
            .static_configs(configs.clone())
            .build();

        let service = RepositoryService::new(Arc::new(RwLock::new(mgr)));

        // Fetch the list of results and make sure the results are what we expected.
        let results = list(&service).await;
        assert_eq!(results, configs);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_insert_list_remove() {
        let dynamic_dir = tempfile::tempdir().unwrap();
        let mgr =
            RepositoryManagerBuilder::new_test(&dynamic_dir, Some("config")).await.unwrap().build();
        let mut service = RepositoryService::new(Arc::new(RwLock::new(mgr)));

        // First, create a bunch of repo configs we're going to use for testing.
        // FIXME: the current implementation ends up writing O(n^2) bytes when serializing the
        // repositories. Raise this number to be greater than LIST_CHUNK_SIZE once serialization
        // is cheaper.
        let configs = (0..20)
            .map(|i| {
                let url =
                    RepositoryUrl::parse(&format!("fuchsia-pkg://fuchsia{:04}.com", i)).unwrap();
                RepositoryConfigBuilder::new(url).build()
            })
            .collect::<Vec<_>>();

        // Insert all the configs and make sure it is successful.
        for config in &configs {
            assert_eq!(service.serve_insert(config.clone().into()).await, Ok(()));
        }

        // Fetch the list of results and make sure the results are what we expected.
        let results = list(&service).await;
        assert_eq!(results, configs);

        // Remove all the configs and make sure nothing is left.
        for config in &configs {
            assert_eq!(service.serve_remove(config.repo_url().to_string()).await, Ok(()));
        }

        // We should now not receive anything.
        let results = list(&service).await;
        assert_eq!(results, vec![]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_insert_fails_with_access_denied_if_disabled() {
        let dynamic_dir = tempfile::tempdir().unwrap();
        let mgr = RepositoryManagerBuilder::new_test(&dynamic_dir, Option::<String>::None)
            .await
            .unwrap()
            .build();
        let mut service = RepositoryService::new(Arc::new(RwLock::new(mgr)));
        let url = RepositoryUrl::parse("fuchsia-pkg://fuchsia.com").unwrap();
        let config = RepositoryConfigBuilder::new(url).build();

        assert_eq!(service.serve_insert(config.into()).await, Err(Status::ACCESS_DENIED));
        assert_eq!(list(&service).await, vec![]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_remove_fails_with_access_denied_if_disabled() {
        let dynamic_dir = tempfile::tempdir().unwrap();
        let mgr = RepositoryManagerBuilder::new_test(&dynamic_dir, Option::<String>::None)
            .await
            .unwrap()
            .build();
        let mut service = RepositoryService::new(Arc::new(RwLock::new(mgr)));

        assert_eq!(
            service.serve_remove("fuchsia-pkg://fuchsia.com".to_string()).await,
            Err(Status::ACCESS_DENIED)
        );
    }
}
