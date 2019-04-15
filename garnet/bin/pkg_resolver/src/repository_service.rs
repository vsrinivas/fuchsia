// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::repository_manager::RepositoryManager;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_pkg::{
    MirrorConfig as FidlMirrorConfig, RepositoryConfig as FidlRepositoryConfig,
    RepositoryIteratorMarker, RepositoryIteratorRequest, RepositoryManagerRequest,
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
                    let status = self.insert(repo);
                    responder.send(Status::from(status).into_raw())?;
                }
                RepositoryManagerRequest::Remove { repo_url, responder } => {
                    let status = self.remove(repo_url);
                    responder.send(Status::from(status).into_raw())?;
                }
                RepositoryManagerRequest::AddMirror { repo_url, mirror, responder } => {
                    let status = self.insert_mirror(repo_url, mirror);
                    responder.send(Status::from(status).into_raw())?;
                }
                RepositoryManagerRequest::RemoveMirror { repo_url, mirror_url, responder } => {
                    let status = self.remove_mirror(repo_url, mirror_url);
                    responder.send(Status::from(status).into_raw())?;
                }
                RepositoryManagerRequest::List { iterator, control_handle: _ } => {
                    self.list(iterator);
                }
            }
        }

        Ok(())
    }

    fn insert(&mut self, _repo: FidlRepositoryConfig) -> Result<(), Status> {
        Err(Status::INTERNAL)
    }

    fn remove(&mut self, _repo_url: String) -> Result<(), Status> {
        Err(Status::INTERNAL)
    }

    fn insert_mirror(
        &mut self,
        _repo_url: String,
        _mirror: FidlMirrorConfig,
    ) -> Result<(), Status> {
        Err(Status::INTERNAL)
    }

    fn remove_mirror(&mut self, _repo_url: String, _mirror_url: String) -> Result<(), Status> {
        Err(Status::INTERNAL)
    }

    fn list(&self, iter: ServerEnd<RepositoryIteratorMarker>) {
        let mut chunks = {
            let repo_manager = self.repo_manager.read();
            let mut results = repo_manager.list().collect::<Vec<_>>();

            results.sort_unstable_by_key(|(k, _)| &**k);

            // This is a bit obnoxious. fidl iterators need to pass along a chunk of
            // ExactSizedIterators that own their items. Unfortunately this is a bit complicated to
            // create. Hypothetically if there was a `Vec::into_chunks` Iterator we could do this
            // in one go, but alas it doesn't exist.
            results
                .chunks(LIST_CHUNK_SIZE)
                .map(|chunk| {
                    chunk
                        .into_iter()
                        .map(|&(_, config)| config.clone().into())
                        .collect::<Vec<FidlRepositoryConfig>>()
                })
                .collect::<Vec<_>>()
                .into_iter()
        };

        fasync::spawn(
            async move {
                let mut stream = iter.into_stream()?;

                while let Some(RepositoryIteratorRequest::Next { responder }) =
                    await!(stream.try_next())?
                {
                    if let Some(chunk) = chunks.next() {
                        responder.send(&mut chunk.into_iter())?;
                    } else {
                        responder.send(&mut vec![].into_iter())?;
                    }
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
    use fidl::endpoints::create_proxy;
    use fidl_fuchsia_pkg_ext::{RepositoryConfig, RepositoryConfigBuilder, RepositoryConfigs};
    use fuchsia_uri::pkg_uri::RepoUri;
    use std::convert::TryInto;

    async fn list(service: &RepositoryService) -> Vec<RepositoryConfig> {
        let (list_iterator, server_end) = create_proxy().unwrap();
        service.list(server_end);

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
        let configs = (0..1000)
            .map(|i| {
                let uri = RepoUri::parse(&format!("fuchsia-pkg://fuchsia{:04}.com", i)).unwrap();
                RepositoryConfigBuilder::new(uri).build()
            })
            .collect::<Vec<_>>();

        let repo_configs = configs
            .iter()
            .map(|c| (c.repo_url().host(), RepositoryConfigs::Version1(vec![c.clone()])))
            .collect::<Vec<_>>();

        let dir = create_dir(repo_configs);

        let (mgr, errors) = RepositoryManager::load_dir(dir).unwrap();
        assert_eq!(errors.len(), 0);

        let service = RepositoryService::new(Arc::new(RwLock::new(mgr)));

        // Fetch the list of results and make sure the results are what we expected.
        let results = await!(list(&service));
        assert_eq!(results, configs);
    }
}
