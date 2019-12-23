// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::amber_connector::AmberConnect;
use crate::repository_manager::{InsertError, RemoveError, RepositoryManager};
use fidl_fuchsia_pkg::{
    MirrorConfig as FidlMirrorConfig, RepositoryConfig as FidlRepositoryConfig,
    RepositoryIteratorRequest, RepositoryIteratorRequestStream, RepositoryManagerRequest,
    RepositoryManagerRequestStream,
};
use fidl_fuchsia_pkg_ext::RepositoryConfig;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use fuchsia_url::pkg_url::RepoUrl;
use fuchsia_zircon::Status;
use futures::prelude::*;
use parking_lot::RwLock;
use std::convert::TryFrom;
use std::sync::Arc;

const LIST_CHUNK_SIZE: usize = 100;

pub struct RepositoryService<A: AmberConnect> {
    repo_manager: Arc<RwLock<RepositoryManager<A>>>,
}

impl<A: AmberConnect> RepositoryService<A> {
    pub fn new(repo_manager: Arc<RwLock<RepositoryManager<A>>>) -> Self {
        RepositoryService { repo_manager: repo_manager }
    }

    pub async fn run(
        &mut self,
        mut stream: RepositoryManagerRequestStream,
    ) -> Result<(), failure::Error> {
        while let Some(event) = stream.try_next().await? {
            match event {
                RepositoryManagerRequest::Add { repo, responder } => {
                    let status = self.serve_insert(repo);
                    responder.send(Status::from(status).into_raw())?;
                }
                RepositoryManagerRequest::Remove { repo_url, responder } => {
                    let status = self.serve_remove(repo_url);
                    responder.send(Status::from(status).into_raw())?;
                }
                RepositoryManagerRequest::AddMirror { repo_url, mirror, responder } => {
                    let status = self.serve_insert_mirror(repo_url, mirror);
                    responder.send(Status::from(status).into_raw())?;
                }
                RepositoryManagerRequest::RemoveMirror { repo_url, mirror_url, responder } => {
                    let status = self.serve_remove_mirror(repo_url, mirror_url);
                    responder.send(Status::from(status).into_raw())?;
                }
                RepositoryManagerRequest::List { iterator, control_handle: _ } => {
                    let stream = iterator.into_stream()?;
                    self.serve_list(stream);
                }
            }
        }

        Ok(())
    }

    fn serve_insert(&mut self, repo: FidlRepositoryConfig) -> Result<(), Status> {
        let repo = match RepositoryConfig::try_from(repo) {
            Ok(repo) => repo,
            Err(err) => {
                fx_log_err!("invalid repository config: {}", err);
                return Err(Status::INVALID_ARGS);
            }
        };

        match self.repo_manager.write().insert(repo) {
            Ok(_) => Ok(()),
            Err(InsertError::DynamicConfigurationDisabled) => Err(Status::ACCESS_DENIED),
        }
    }

    fn serve_remove(&mut self, repo_url: String) -> Result<(), Status> {
        let repo_url = match RepoUrl::parse(&repo_url) {
            Ok(repo_url) => repo_url,
            Err(err) => {
                fx_log_err!("invalid repository URL: {}", err);
                return Err(Status::INVALID_ARGS);
            }
        };

        match self.repo_manager.write().remove(&repo_url) {
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

    fn serve_list(&self, mut stream: RepositoryIteratorRequestStream) {
        let results = self
            .repo_manager
            .read()
            .list()
            .map(|config| config.clone().into())
            .collect::<Vec<FidlRepositoryConfig>>();

        fasync::spawn(
            async move {
                let mut iter = results.into_iter();

                while let Some(RepositoryIteratorRequest::Next { responder }) =
                    stream.try_next().await?
                {
                    responder.send(&mut iter.by_ref().take(LIST_CHUNK_SIZE))?;
                }
                Ok(())
            }
            .unwrap_or_else(|e: failure::Error| {
                fx_log_err!("error running list protocol: {:?}", e)
            }),
        );
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::repository_manager::RepositoryManagerBuilder,
        crate::test_util::ClosedAmberConnector,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_pkg::RepositoryIteratorMarker,
        fidl_fuchsia_pkg_ext::{RepositoryConfig, RepositoryConfigBuilder},
        fuchsia_url::pkg_url::RepoUrl,
        std::{convert::TryInto, path::Path},
    };

    async fn list<A>(service: &RepositoryService<A>) -> Vec<RepositoryConfig>
    where
        A: AmberConnect,
    {
        let (list_iterator, stream) =
            create_proxy_and_stream::<RepositoryIteratorMarker>().unwrap();
        service.serve_list(stream);

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
        let dynamic_configs_path = dynamic_dir.path().join("config");
        let mgr =
            RepositoryManagerBuilder::new_test(Some(&dynamic_configs_path), ClosedAmberConnector)
                .unwrap()
                .build();
        let service = RepositoryService::new(Arc::new(RwLock::new(mgr)));

        let results = list(&service).await;
        assert_eq!(results, vec![]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list() {
        // First, create a bunch of repo configs we're going to use for testing.
        let configs = (0..200)
            .map(|i| {
                let url = RepoUrl::parse(&format!("fuchsia-pkg://fuchsia{:04}.com", i)).unwrap();
                RepositoryConfigBuilder::new(url).build()
            })
            .collect::<Vec<_>>();

        let dynamic_dir = tempfile::tempdir().unwrap();
        let dynamic_configs_path = dynamic_dir.path().join("config");
        let mgr =
            RepositoryManagerBuilder::new_test(Some(&dynamic_configs_path), ClosedAmberConnector)
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
        let dynamic_configs_path = dynamic_dir.path().join("config");
        let mgr =
            RepositoryManagerBuilder::new_test(Some(&dynamic_configs_path), ClosedAmberConnector)
                .unwrap()
                .build();
        let mut service = RepositoryService::new(Arc::new(RwLock::new(mgr)));

        // First, create a bunch of repo configs we're going to use for testing.
        // FIXME: the current implementation ends up writing O(n^2) bytes when serializing the
        // repositories. Raise this number to be greater than LIST_CHUNK_SIZE once serialization
        // is cheaper.
        let configs = (0..20)
            .map(|i| {
                let url = RepoUrl::parse(&format!("fuchsia-pkg://fuchsia{:04}.com", i)).unwrap();
                RepositoryConfigBuilder::new(url).build()
            })
            .collect::<Vec<_>>();

        // Insert all the configs and make sure it is successful.
        for config in &configs {
            assert_eq!(service.serve_insert(config.clone().into()), Ok(()));
        }

        // Fetch the list of results and make sure the results are what we expected.
        let results = list(&service).await;
        assert_eq!(results, configs);

        // Remove all the configs and make sure nothing is left.
        for config in &configs {
            assert_eq!(service.serve_remove(config.repo_url().to_string()), Ok(()));
        }

        // We should now not receive anything.
        let results = list(&service).await;
        assert_eq!(results, vec![]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_insert_fails_with_access_denied_if_disabled() {
        let mgr = RepositoryManagerBuilder::new_test(Option::<&Path>::None, ClosedAmberConnector)
            .unwrap()
            .build();
        let mut service = RepositoryService::new(Arc::new(RwLock::new(mgr)));
        let url = RepoUrl::parse("fuchsia-pkg://fuchsia.com").unwrap();
        let config = RepositoryConfigBuilder::new(url).build();

        assert_eq!(service.serve_insert(config.into()), Err(Status::ACCESS_DENIED));
        assert_eq!(list(&service).await, vec![]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_remove_fails_with_access_denied_if_disabled() {
        let mgr = RepositoryManagerBuilder::new_test(Option::<&Path>::None, ClosedAmberConnector)
            .unwrap()
            .build();
        let mut service = RepositoryService::new(Arc::new(RwLock::new(mgr)));

        assert_eq!(
            service.serve_remove("fuchsia-pkg://fuchsia.com".to_string()),
            Err(Status::ACCESS_DENIED)
        );
    }
}
