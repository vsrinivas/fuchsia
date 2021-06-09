// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{anyhow, Result},
    async_lock::RwLock,
    async_trait::async_trait,
    ffx_config::{self, ConfigLevel},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_bridge as bridge,
    fidl_fuchsia_net::IpAddress,
    fidl_fuchsia_pkg::RepositoryManagerMarker,
    fidl_fuchsia_pkg_rewrite::{EngineMarker, LiteralRule, Rule},
    fuchsia_async::{self as fasync, futures::StreamExt as _},
    pkg::repository::{
        FileSystemRepository, Repository, RepositoryManager, RepositoryServer, RepositorySpec,
    },
    serde_json,
    services::prelude::*,
    std::{net, sync::Arc},
};

const REPOSITORY_MANAGER_SELECTOR: &str = "core/appmgr:out:fuchsia.pkg.RepositoryManager";
const REWRITE_SERVICE_SELECTOR: &str = "core/appmgr:out:fuchsia.pkg.rewrite.Engine";

struct ServerInfo {
    server: RepositoryServer,
    addr: net::SocketAddr,
    task: fasync::Task<()>,
}

#[ffx_service]
pub struct Repo {
    manager: Arc<RepositoryManager>,
    server: RwLock<Option<ServerInfo>>,
}

impl Repo {
    async fn start_server(&self, addr: net::SocketAddr) -> bool {
        let mut server_locked = self.server.write().await;
        match &*server_locked {
            Some(x) if x.addr == addr => true,
            _ => {
                *server_locked = None;

                log::info!("Trying to serve on {}", addr);
                if let Err(x) = ffx_config::set(
                    ("repository_server.listen", ConfigLevel::User),
                    format!("{}", addr).into(),
                )
                .await
                {
                    log::warn!("Could not save address to config: {}", x);
                }

                match RepositoryServer::builder(addr, Arc::clone(&self.manager)).start().await {
                    Ok((task, server)) => {
                        let task = fasync::Task::local(task);
                        log::info!("Started server on {}", server.local_addr());
                        *server_locked = Some(ServerInfo { server, addr, task });
                        true
                    }
                    Err(x) => {
                        log::warn!("Could not start server: {}", x);
                        false
                    }
                }
            }
        }
    }

    async fn register_target(
        &self,
        cx: &Context,
        target_info: bridge::RepositoryTarget,
        responder: bridge::RepositoriesRegisterTargetResponder,
    ) -> Result<()> {
        let repo_opt = if let Some(repo_name) = target_info.repo_name {
            self.manager.get(&repo_name)
        } else {
            responder.send(&mut Err(bridge::RepositoryError::MissingRepositoryName))?;
            return Ok(());
        };

        let repo = if let Some(repo) = repo_opt {
            repo
        } else {
            responder.send(&mut Err(bridge::RepositoryError::NoMatchingRepository))?;
            return Ok(());
        };

        let proxy = match cx
            .open_target_proxy::<RepositoryManagerMarker>(
                target_info.target_identifier.clone(),
                REPOSITORY_MANAGER_SELECTOR,
            )
            .await
        {
            Ok(p) => p,
            Err(e) => {
                log::warn!(
                    "failed to open target proxy with target name '{:?}'. Error was: {}",
                    target_info.target_identifier,
                    e
                );
                responder.send(&mut Err(bridge::RepositoryError::TargetCommunicationFailure))?;
                return Ok(());
            }
        };

        // TODO(fxbug.dev/77015): parameterize the mirror_url value here once we are dynamically assigning ports.
        let config = repo.get_config(&format!("localhost:8084/{}", repo.name())).await?;
        match proxy.add(config.into()).await {
            Ok(result) => {
                if let Err(s) = result {
                    log::warn!("failed to add config. Status was: {}", s);
                    responder.send(&mut Err(bridge::RepositoryError::RepositoryManagerError))?;
                    return Ok(());
                }
            }
            Err(e) => {
                log::warn!("failed to add config. Error was: {}", e);
                responder.send(&mut Err(bridge::RepositoryError::TargetCommunicationFailure))?;
                return Ok(());
            }
        }

        let aliases = target_info.aliases.unwrap_or(vec![]);
        if aliases.is_empty() {
            responder.send(&mut Ok(()))?;
            return Ok(());
        }

        let rewrite_proxy = match cx
            .open_target_proxy::<EngineMarker>(
                target_info.target_identifier.clone(),
                REWRITE_SERVICE_SELECTOR,
            )
            .await
        {
            Ok(p) => p,
            Err(e) => {
                log::warn!(
                            "failed to open Rewrite Engine target proxy with target name '{:?}'. Error was: {}",
                            target_info.target_identifier,
                            e
                        );
                responder.send(&mut Err(bridge::RepositoryError::TargetCommunicationFailure))?;
                return Ok(());
            }
        };

        let (transaction_proxy, server_end) = create_proxy()?;
        rewrite_proxy.start_edit_transaction(server_end)?;

        for alias in aliases.iter() {
            let rule = LiteralRule {
                host_match: alias.to_string(),
                host_replacement: repo.name().to_string(),
                path_prefix_match: "/".to_string(),
                path_prefix_replacement: "/".to_string(),
            };
            match transaction_proxy.add(&mut Rule::Literal(rule)).await {
                Err(e) => {
                    log::warn!("failed to add rewrite rule. Error was: {}", e);
                    responder.send(&mut Err(bridge::RepositoryError::RewriteEngineError))?;
                    return Ok(());
                }
                Ok(Err(e)) => {
                    log::warn!("Adding rewrite rule returned failure. Error was: {}", e);
                    responder.send(&mut Err(bridge::RepositoryError::RewriteEngineError))?;
                    return Ok(());
                }
                Ok(_) => {}
            }
        }
        match transaction_proxy.commit().await {
            Err(e) => {
                log::warn!("failed to commit rewrite rule. Error was: {}", e);
                responder.send(&mut Err(bridge::RepositoryError::RewriteEngineError))?;
                return Ok(());
            }
            Ok(Err(e)) => {
                log::warn!("Committing rewrite rule returned failure. Error was: {}", e);
                responder.send(&mut Err(bridge::RepositoryError::RewriteEngineError))?;
                return Ok(());
            }
            Ok(_) => {}
        }

        responder.send(&mut Ok(()))?;
        Ok(())
    }
}

impl Default for Repo {
    fn default() -> Self {
        Repo { manager: RepositoryManager::new(), server: RwLock::new(None) }
    }
}

#[async_trait(?Send)]
impl FidlService for Repo {
    type Service = bridge::RepositoriesMarker;
    type StreamHandler = FidlStreamHandler<Self>;

    async fn handle(&self, cx: &Context, req: bridge::RepositoriesRequest) -> Result<()> {
        match req {
            bridge::RepositoriesRequest::Serve { addr, port, responder } => {
                let addr = match addr {
                    IpAddress::Ipv4(addr) => net::IpAddr::V4(addr.addr.into()),
                    IpAddress::Ipv6(addr) => net::IpAddr::V6(addr.addr.into()),
                };

                let addr = net::SocketAddr::new(addr, port);

                responder.send(self.start_server(addr).await)?;
                Ok(())
            }
            bridge::RepositoriesRequest::Add { name, repository, .. } => {
                log::info!("Adding repository {} {:?}", name, repository);

                match repository {
                    bridge::RepositorySpec::Filesystem(path) => {
                        if let Some(path) = path.path {
                            let err = ffx_config::set(
                                (
                                    format!("repository_server.repositories.{}.type", name)
                                        .as_str(),
                                    ConfigLevel::User,
                                ),
                                "fs".to_owned().into(),
                            )
                            .await
                            .err();
                            let err = if err.is_some() {
                                err
                            } else {
                                ffx_config::set(
                                    (
                                        format!("repository_server.repositories.{}.path", name)
                                            .as_str(),
                                        ConfigLevel::User,
                                    ),
                                    path.clone().into(),
                                )
                                .await
                                .err()
                            };

                            if let Some(err) = err {
                                log::warn!("Could not save repository info to config: {}", err);
                            }

                            match () {
                                #[cfg(test)]
                                () if path.starts_with("test_path") => {
                                    use pkg::repository::RepositoryMetadata;

                                    self.manager.add(Arc::new(Repository::new_with_metadata(
                                        &name,
                                        Box::new(FileSystemRepository::new(path.into())),
                                        RepositoryMetadata::new(Vec::new(), 1),
                                    )))
                                }
                                _ => self.manager.add(Arc::new(
                                    Repository::new(
                                        &name,
                                        Box::new(FileSystemRepository::new(path.into())),
                                    )
                                    .await
                                    .map_err(|x| anyhow!("{:?}", x))?,
                                )),
                            }
                        } else {
                            log::warn!(
                                "Could not save repository info to config: \
                                        Malformed FileSystemRepositorySpec did not contain a path."
                            );
                        }
                    }
                    bridge::RepositorySpecUnknown!() => {
                        log::error!("Unknown RepositorySpec format.");
                    }
                }

                Ok(())
            }
            bridge::RepositoriesRequest::Remove { name, responder } => {
                log::info!("Removing repository {}", name);

                responder.send(self.manager.remove(&name))?;
                Ok(())
            }
            bridge::RepositoriesRequest::RegisterTarget { target_info, responder } => {
                self.register_target(cx, target_info, responder).await
            }
            bridge::RepositoriesRequest::List { iterator, .. } => {
                let mut stream = iterator.into_stream()?;
                let mut values = self
                    .manager
                    .repositories()
                    .map(|x| bridge::RepositoryConfig {
                        name: x.name().to_owned(),
                        spec: match x.spec() {
                            RepositorySpec::FileSystem { path } => {
                                bridge::RepositorySpec::Filesystem(
                                    bridge::FileSystemRepositorySpec {
                                        path: path.as_os_str().to_str().map(|x| x.to_owned()),
                                        ..bridge::FileSystemRepositorySpec::EMPTY
                                    },
                                )
                            }
                        },
                    })
                    .collect::<Vec<_>>();
                let mut pos = 0;

                fasync::Task::spawn(async move {
                    while let Some(request) = stream.next().await {
                        match request {
                            Ok(bridge::RepositoryIteratorRequest::Next { responder }) => {
                                let len = values.len();
                                let chunk = &mut values[pos..]
                                    [..std::cmp::min(len - pos, bridge::MAX_REPOS as usize)]
                                    .iter_mut();
                                pos += bridge::MAX_REPOS as usize;
                                pos = std::cmp::min(pos, len);

                                if let Err(e) = responder.send(chunk) {
                                    log::warn!(
                                        "Error responding to RepositoryIterator request: {}",
                                        e
                                    );
                                }
                            }
                            Err(e) => {
                                log::warn!("Error in RepositoryIterator request stream: {}", e)
                            }
                        }
                    }
                })
                .detach();
                Ok(())
            }
        }
    }

    async fn start(&mut self, _cx: &Context) -> Result<()> {
        log::info!("started repository service");

        if let Ok(address) = ffx_config::get::<String, _>("repository_server.listen").await {
            if let Ok(address) = address.parse::<net::SocketAddr>() {
                self.start_server(address).await;
            } else {
                log::warn!("Invalid value for repository_server.listen")
            }
        }

        if let Ok(repos) = ffx_config::get::<serde_json::Map<String, serde_json::Value>, _>(
            "repository_server.repositories",
        )
        .await
        {
            for (name, entry) in repos.into_iter() {
                if let Err(x) = self.manager.add_from_config(name.clone(), entry.clone()).await {
                    log::warn!("While adding repository \"{}\": {:?}", name, x)
                }
            }
        }

        Ok(())
    }

    async fn stop(&mut self, _cx: &Context) -> Result<()> {
        let server_info = self.server.write().await.take();
        if let Some(server_info) = server_info {
            server_info.server.stop();

            // TODO(sadmac): Timeout.
            server_info.task.await;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl,
        fidl_fuchsia_pkg::{MirrorConfig, RepositoryConfig, RepositoryManagerRequest},
        fidl_fuchsia_pkg_rewrite::{EditTransactionRequest, EngineMarker, EngineRequest},
    };

    const REPO_NAME: &str = "some_repo";
    #[derive(Default)]
    struct FakeRepositoryManager {}

    #[async_trait(?Send)]
    impl FidlService for FakeRepositoryManager {
        type Service = RepositoryManagerMarker;
        type StreamHandler = FidlStreamHandler<Self>;

        async fn handle(&self, _cx: &Context, req: RepositoryManagerRequest) -> Result<()> {
            match req {
                RepositoryManagerRequest::Add { repo, responder } => {
                    assert_eq!(
                        repo,
                        RepositoryConfig {
                            repo_url: Some(format!("fuchsia-pkg://{}", REPO_NAME)),
                            mirrors: Some(vec![MirrorConfig {
                                mirror_url: Some(format!("http://localhost:8084/{}", REPO_NAME)),
                                subscribe: Some(false),
                                ..MirrorConfig::EMPTY
                            }]),
                            root_keys: Some(vec![]),
                            root_version: Some(1),
                            ..RepositoryConfig::EMPTY
                        }
                    );
                    responder.send(&mut Ok(())).unwrap()
                }
                _ => {
                    panic!("unexpected RepositoryManager request {:?}", req);
                }
            }
            Ok(())
        }
    }

    #[derive(Default)]
    struct ErroringRepositoryManager {}

    #[async_trait(?Send)]
    impl FidlService for ErroringRepositoryManager {
        type Service = RepositoryManagerMarker;
        type StreamHandler = FidlStreamHandler<Self>;

        async fn handle(&self, _cx: &Context, req: RepositoryManagerRequest) -> Result<()> {
            match req {
                RepositoryManagerRequest::Add { repo, responder } => {
                    assert_eq!(
                        repo,
                        RepositoryConfig {
                            repo_url: Some(format!("fuchsia-pkg://{}", REPO_NAME)),
                            mirrors: Some(vec![MirrorConfig {
                                mirror_url: Some(format!("http://localhost:8084/{}", REPO_NAME)),
                                subscribe: Some(false),
                                ..MirrorConfig::EMPTY
                            }]),
                            root_keys: Some(vec![]),
                            root_version: Some(1),
                            ..RepositoryConfig::EMPTY
                        }
                    );

                    responder.send(&mut Err(1)).unwrap()
                }
                _ => {
                    panic!("unexpected RepositoryManager request {:?}", req);
                }
            }
            Ok(())
        }
    }

    #[derive(Default)]
    struct FakeEngine {}
    #[async_trait(?Send)]
    impl FidlService for FakeEngine {
        type Service = EngineMarker;
        type StreamHandler = FidlStreamHandler<Self>;

        async fn handle(&self, _cx: &Context, req: EngineRequest) -> Result<()> {
            match req {
                EngineRequest::StartEditTransaction { transaction, .. } => {
                    let mut stream = transaction.into_stream().unwrap();
                    while let Some(request) = stream.next().await {
                        let request = request.unwrap();
                        match request {
                            EditTransactionRequest::Add { rule, responder } => {
                                assert_eq!(
                                    rule,
                                    Rule::Literal(LiteralRule {
                                        host_match: "fuchsia.com".to_string(),
                                        host_replacement: REPO_NAME.to_string(),
                                        path_prefix_match: "/".to_string(),
                                        path_prefix_replacement: "/".to_string(),
                                    })
                                );
                                responder.send(&mut Ok(())).unwrap()
                            }

                            EditTransactionRequest::Commit { responder } => {
                                responder.send(&mut Ok(())).unwrap()
                            }
                            _ => {
                                panic!("unexpected EditTransaction request");
                            }
                        }
                    }
                }
                _ => {
                    panic!("unexpected Engine request {:?}", req);
                }
            }
            Ok(())
        }
    }

    async fn add_repo(daemon: &FakeDaemon) -> bridge::RepositoriesProxy {
        let proxy = daemon.open_proxy::<bridge::RepositoriesMarker>().await;
        let spec = bridge::RepositorySpec::Filesystem(bridge::FileSystemRepositorySpec {
            path: Some("test_path/bar".to_owned()),
            ..bridge::FileSystemRepositorySpec::EMPTY
        });
        proxy.add(REPO_NAME, &mut spec.clone()).unwrap();

        proxy
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_remove() {
        let daemon = FakeDaemonBuilder::new().register_fidl_service::<Repo>().build();
        let proxy = daemon.open_proxy::<bridge::RepositoriesMarker>().await;
        let spec = bridge::RepositorySpec::Filesystem(bridge::FileSystemRepositorySpec {
            path: Some("test_path/bar".to_owned()),
            ..bridge::FileSystemRepositorySpec::EMPTY
        });
        proxy.add(REPO_NAME, &mut spec.clone()).unwrap();

        let (client, server) = fidl::endpoints::create_endpoints().unwrap();
        proxy.list(server).unwrap();
        let client = client.into_proxy().unwrap();

        let next = client.next().await.unwrap();

        assert_eq!(1, next.len());
        assert_eq!(0, client.next().await.unwrap().len());

        let got = &next[0];
        assert_eq!(REPO_NAME, &got.name);
        assert_eq!(spec, got.spec);

        assert!(proxy.remove(REPO_NAME).await.unwrap());

        let (client, server) = fidl::endpoints::create_endpoints().unwrap();
        proxy.list(server).unwrap();
        let client = client.into_proxy().unwrap();

        assert_eq!(0, client.next().await.unwrap().len());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_register() {
        let daemon = FakeDaemonBuilder::new()
            .register_fidl_service::<FakeRepositoryManager>()
            .register_fidl_service::<FakeEngine>()
            .register_fidl_service::<Repo>()
            .build();

        let proxy = add_repo(&daemon).await;
        proxy
            .register_target(bridge::RepositoryTarget {
                repo_name: Some(REPO_NAME.to_string()),
                target_identifier: None,
                aliases: Some(vec!["fuchsia.com".to_string()]),
                ..bridge::RepositoryTarget::EMPTY
            })
            .await
            .unwrap()
            .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_register_empty_aliases() {
        let daemon = FakeDaemonBuilder::new()
            .register_fidl_service::<FakeRepositoryManager>()
            .register_fidl_service::<FakeEngine>()
            .register_fidl_service::<Repo>()
            .build();

        let proxy = add_repo(&daemon).await;
        proxy
            .register_target(bridge::RepositoryTarget {
                repo_name: Some(REPO_NAME.to_string()),
                target_identifier: None,
                aliases: Some(vec![]),
                ..bridge::RepositoryTarget::EMPTY
            })
            .await
            .unwrap()
            .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_register_none_aliases() {
        let daemon = FakeDaemonBuilder::new()
            .register_fidl_service::<FakeRepositoryManager>()
            .register_fidl_service::<FakeEngine>()
            .register_fidl_service::<Repo>()
            .build();

        let proxy = add_repo(&daemon).await;
        proxy
            .register_target(bridge::RepositoryTarget {
                repo_name: Some(REPO_NAME.to_string()),
                target_identifier: None,
                aliases: None,
                ..bridge::RepositoryTarget::EMPTY
            })
            .await
            .unwrap()
            .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_register_repo_manager_error() {
        let daemon = FakeDaemonBuilder::new()
            .register_fidl_service::<ErroringRepositoryManager>()
            .register_fidl_service::<FakeEngine>()
            .register_fidl_service::<Repo>()
            .build();

        let proxy = add_repo(&daemon).await;
        assert_eq!(
            proxy
                .register_target(bridge::RepositoryTarget {
                    repo_name: Some(REPO_NAME.to_string()),
                    target_identifier: None,
                    aliases: None,
                    ..bridge::RepositoryTarget::EMPTY
                })
                .await
                .unwrap()
                .unwrap_err(),
            bridge::RepositoryError::RepositoryManagerError
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_register_non_existent_repo() {
        let daemon = FakeDaemonBuilder::new()
            .register_fidl_service::<ErroringRepositoryManager>()
            .register_fidl_service::<FakeEngine>()
            .register_fidl_service::<Repo>()
            .build();

        let proxy = daemon.open_proxy::<bridge::RepositoriesMarker>().await;
        assert_eq!(
            proxy
                .register_target(bridge::RepositoryTarget {
                    repo_name: Some(REPO_NAME.to_string()),
                    target_identifier: None,
                    aliases: None,
                    ..bridge::RepositoryTarget::EMPTY
                })
                .await
                .unwrap()
                .unwrap_err(),
            bridge::RepositoryError::NoMatchingRepository
        );
    }
}
