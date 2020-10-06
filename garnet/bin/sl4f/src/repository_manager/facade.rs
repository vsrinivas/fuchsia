// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::get_proxy_or_connect;
use crate::repository_manager::types::RepositoryOutput;

use anyhow::{format_err, Error};
use fidl_fuchsia_pkg::{RepositoryManagerMarker, RepositoryManagerProxy};
use fidl_fuchsia_pkg_ext::RepositoryConfig;
use fuchsia_syslog::macros::fx_log_info;
use fuchsia_zircon as zx;
use parking_lot::RwLock;
use serde_json::{from_value, to_value, Value};
use std::convert::TryFrom;

/// Facade providing access to RepositoryManager interfaces.
#[derive(Debug)]
pub struct RepositoryManagerFacade {
    proxy: RwLock<Option<RepositoryManagerProxy>>,
}

impl RepositoryManagerFacade {
    pub fn new() -> Self {
        Self { proxy: RwLock::new(None) }
    }

    #[cfg(test)]
    fn new_with_proxy(proxy: RepositoryManagerProxy) -> Self {
        Self { proxy: RwLock::new(Some(proxy)) }
    }

    fn proxy(&self) -> Result<RepositoryManagerProxy, Error> {
        get_proxy_or_connect::<RepositoryManagerMarker>(&self.proxy)
    }

    /// Lists repositories using the repository_manager fidl service.
    ///
    /// Returns a list containing repository info in the format of
    /// RepositoryConfig.
    pub async fn list_repo(&self) -> Result<Value, Error> {
        match self.fetch_repos().await {
            Ok(repos) => {
                let return_value = to_value(&repos)?;
                return Ok(return_value);
            }
            Err(err) => {
                return Err(format_err!("Listing Repositories failed with error {:?}", err))
            }
        };
    }

    /// Add a new source to an existing repository.
    ///
    /// params format uses RepositoryConfig, example:
    /// {
    ///     "repo_url": "fuchsia-pkg://example.com",
    ///     "root_keys":[
    ///         {
    ///             "type":"ed25519",
    ///             "value":"00"
    ///         }],
    ///     "mirrors": [
    ///         {
    ///             "mirror_url": "http://example.org/",
    ///             "subscribe": true
    ///         }],
    ///     "update_package_url": "fuchsia-pkg://update.example.com/update",
    ///     "root_version": 1,
    ///     "root_threshold": 1,
    /// }
    pub async fn add(&self, args: Value) -> Result<Value, Error> {
        let add_request: RepositoryConfig = from_value(args)?;
        fx_log_info!("Add Repo request received {:?}", add_request);

        let res = self.proxy()?.add(add_request.into()).await?;
        match res.map_err(zx::Status::from_raw) {
            Ok(()) => Ok(to_value(RepositoryOutput::Success)?),
            _ => Err(format_err!("Add repo errored with code {:?}", res)),
        }
    }

    /// Fetches repositories using repository_manager.list FIDL service.
    async fn fetch_repos(&self) -> Result<Vec<RepositoryConfig>, anyhow::Error> {
        let (iter, server_end) = fidl::endpoints::create_proxy()?;
        self.proxy()?.list(server_end)?;
        let mut repos = vec![];

        loop {
            let chunk = iter.next().await?;
            if chunk.is_empty() {
                break;
            }
            repos.extend(chunk);
        }
        repos
            .into_iter()
            .map(|repo| RepositoryConfig::try_from(repo).map_err(|e| anyhow::Error::from(e)))
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common_utils::test::assert_value_round_trips_as;
    use fidl_fuchsia_pkg::{RepositoryIteratorRequest, RepositoryManagerRequest};
    use fidl_fuchsia_pkg_ext::{
        MirrorConfigBuilder, RepositoryConfig, RepositoryConfigBuilder, RepositoryKey,
    };
    use fuchsia_syslog::macros::{fx_log_err, fx_log_info};
    use fuchsia_url::pkg_url::{PkgUrl, RepoUrl};
    use futures::{future::Future, join, StreamExt, TryFutureExt, TryStreamExt};
    use http::Uri;
    use matches::assert_matches;
    use parking_lot::Mutex;
    use serde_json::json;
    use std::iter::FusedIterator;

    fn make_test_repo_config() -> RepositoryConfig {
        RepositoryConfigBuilder::new(RepoUrl::new("example.com".to_string()).expect("valid url"))
            .add_root_key(RepositoryKey::Ed25519(vec![0u8]))
            .add_mirror(
                MirrorConfigBuilder::new("http://example.org".parse::<Uri>().unwrap())
                    .unwrap()
                    .subscribe(true)
                    .build(),
            )
            .update_package_url(
                PkgUrl::parse("fuchsia-pkg://update.example.com/update").expect("valid PkgUrl"),
            )
            .build()
    }

    struct MockRepositoryManagerBuilder {
        expected: Vec<Box<dyn FnOnce(RepositoryManagerRequest) + Send + 'static>>,
        repos: Mutex<Vec<RepositoryConfig>>,
    }

    impl MockRepositoryManagerBuilder {
        fn new() -> Self {
            Self { expected: vec![], repos: Mutex::new(vec![]) }
        }

        fn push(mut self, request: impl FnOnce(RepositoryManagerRequest) + Send + 'static) -> Self {
            self.expected.push(Box::new(request));
            self
        }

        fn add_repository(self, repo_config: RepositoryConfig) -> Self {
            self.repos.lock().push(repo_config);
            self
        }

        fn expect_list_repository(self) -> Self {
            let mut repos = self.repos.lock().clone().into_iter().map(|r| r.into());
            self.push(move |req| match req {
                RepositoryManagerRequest::List { iterator, .. } => {
                    let mut stream = iterator.into_stream().expect("list iterator into_stream");
                    // repos must be fused b/c the Next() fidl method should return an empty vector
                    // forever after iteration is complete
                    let _: &dyn FusedIterator<Item = _> = &repos;
                    fuchsia_async::Task::spawn(
                        async move {
                            while let Some(RepositoryIteratorRequest::Next { responder }) =
                                stream.try_next().await?
                            {
                                responder.send(&mut repos.by_ref().take(5)).expect("next send")
                            }
                            Ok(())
                        }
                        .unwrap_or_else(|e: anyhow::Error| {
                            fx_log_err!("error running list protocol: {:#}", e)
                        }),
                    )
                    .detach();
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_add_repository(self, repo_add: RepositoryConfig) -> Self {
            self.push(move |req| match req {
                RepositoryManagerRequest::Add { repo, responder } => {
                    let new_repo = RepositoryConfig::try_from(repo).expect("valid repo config");
                    assert_eq!(new_repo, repo_add);
                    responder.send(&mut Ok(())).expect("send ok");
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn build(self) -> (RepositoryManagerFacade, impl Future<Output = ()>) {
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<RepositoryManagerMarker>().unwrap();
            let fut = async move {
                for expected in self.expected {
                    expected(stream.next().await.unwrap().unwrap());
                }
                assert_matches!(stream.next().await, None);
            };

            (RepositoryManagerFacade::new_with_proxy(proxy), fut)
        }
    }

    #[test]
    fn serde_repo_configuration() {
        let repo_config = make_test_repo_config();
        assert_value_round_trips_as(
            repo_config,
            json!(
            {
                "repo_url": "fuchsia-pkg://example.com",
                "root_keys":[
                    {
                        "type":"ed25519",
                        "value":"00"
                    }],
                "mirrors": [
                    {
                        "mirror_url": "http://example.org/",
                        "subscribe": true
                    }],
                "update_package_url": "fuchsia-pkg://update.example.com/update",
                "root_version": 1,
                "root_threshold": 1,
                "use_local_mirror": false,
                "repo_storage_type": "ephemeral",
            }),
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn list_repository_ok() {
        let (facade, repository_manager) = MockRepositoryManagerBuilder::new()
            .add_repository(make_test_repo_config())
            .expect_list_repository()
            .build();

        let test = async move {
            let config = facade.list_repo().await.unwrap();
            fx_log_info!("Repo listed: {:?}", config);
            let mut repo_config: Vec<RepositoryConfig> = from_value(config).unwrap();
            assert_eq!(repo_config.len(), 1);

            let received_repo = repo_config.pop().unwrap();
            let expected_pkg_url =
                PkgUrl::parse("fuchsia-pkg://update.example.com/update").unwrap();
            match received_repo.update_package_url() {
                Some(u) => assert_eq!(u.to_string(), expected_pkg_url.to_string()),
                None => fx_log_err!("update_package_url is empty."),
            }
        };
        join!(repository_manager, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn add_repository_ok() {
        let repo_test = make_test_repo_config();
        let (facade, repository_manager) =
            MockRepositoryManagerBuilder::new().expect_add_repository(repo_test.clone()).build();

        let test = async move {
            let status = facade.add(to_value(repo_test.clone()).unwrap()).await.unwrap();
            assert_matches!(from_value(status).unwrap(), RepositoryOutput::Success)
        };
        join!(repository_manager, test);
    }
}
