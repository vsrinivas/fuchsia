// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::repository_manager::RepositoryManager;
use fidl_fuchsia_pkg::{
    MirrorConfig as FidlMirrorConfig, RepositoryConfig as FidlRepositoryConfig,
    RepositoryIteratorRequest, RepositoryIteratorRequestStream, RepositoryManagerRequest,
    RepositoryManagerRequestStream,
};
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use fuchsia_zircon::Status;
use futures::prelude::*;
use futures::TryFutureExt;
use parking_lot::RwLock;
use std::sync::Arc;

const LIST_CHUNK_SIZE: usize = 100;

pub struct RepositoryService {
    repo_manager: Arc<RwLock<RepositoryManager>>,
}

impl RepositoryService {
    pub fn new(repo_manager: Arc<RwLock<RepositoryManager>>) -> Self {
        RepositoryService { repo_manager: repo_manager }
    }

    pub async fn run(
        &mut self,
        mut stream: RepositoryManagerRequestStream,
    ) -> Result<(), failure::Error> {
        while let Some(event) = await!(stream.try_next())? {
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

    fn serve_insert(&mut self, _repo: FidlRepositoryConfig) -> Result<(), Status> {
        Err(Status::INTERNAL)
    }

    fn serve_remove(&mut self, _repo_url: String) -> Result<(), Status> {
        Err(Status::INTERNAL)
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
        let mut results = self
            .repo_manager
            .read()
            .list()
            .map(|(_, config)| config.clone().into())
            .collect::<Vec<FidlRepositoryConfig>>();

        results.sort_unstable_by(|a, b| a.repo_url.cmp(&b.repo_url));

        fasync::spawn(
            async move {
                let mut iter = results.into_iter();

                while let Some(RepositoryIteratorRequest::Next { responder }) =
                    await!(stream.try_next())?
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
    use super::*;
    use crate::test_util::create_dir;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_pkg::RepositoryIteratorMarker;
    use fidl_fuchsia_pkg_ext::{RepositoryConfig, RepositoryConfigBuilder, RepositoryConfigs};
    use fuchsia_uri::pkg_uri::RepoUri;
    use std::convert::TryInto;

    async fn list(service: &RepositoryService) -> Vec<RepositoryConfig> {
        let (list_iterator, stream) =
            create_proxy_and_stream::<RepositoryIteratorMarker>().unwrap();
        service.serve_list(stream);

        let mut results: Vec<RepositoryConfig> = Vec::new();
        loop {
            let chunk = await!(list_iterator.next()).unwrap();
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
        let mgr = Arc::new(RwLock::new(RepositoryManager::new()));
        let service = RepositoryService::new(mgr);

        let results = await!(list(&service));
        assert_eq!(results, vec![]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list() {
        // First, create a bunch of repo configs we're going to use for testing.
        let configs = (0..200)
            .map(|i| {
                let uri = RepoUri::parse(&format!("fuchsia-pkg://fuchsia{:04}.com", i)).unwrap();
                RepositoryConfigBuilder::new(uri).build()
            })
            .collect::<Vec<_>>();

        let dir = create_dir(vec![("configs", RepositoryConfigs::Version1(configs.clone()))]);

        let (mgr, errors) = RepositoryManager::load_dir(dir).unwrap();
        assert_eq!(errors.len(), 0);

        let service = RepositoryService::new(Arc::new(RwLock::new(mgr)));

        // Fetch the list of results and make sure the results are what we expected.
        let results = await!(list(&service));
        assert_eq!(results, configs);
    }
}
