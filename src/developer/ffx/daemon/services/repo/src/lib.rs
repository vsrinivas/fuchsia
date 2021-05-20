// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{anyhow, Result},
    async_lock::RwLock,
    async_trait::async_trait,
    ffx_config::{self, ConfigLevel},
    fidl_fuchsia_developer_bridge as bridge,
    fidl_fuchsia_net::IpAddress,
    fuchsia_async::{self as fasync, futures::StreamExt as _},
    pkg::repository::{
        FileSystemRepository, Repository, RepositoryManager, RepositoryServer, RepositorySpec,
    },
    serde_json,
    services::prelude::*,
    std::{net, sync::Arc},
};

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

    async fn handle(&self, _cx: &Context, req: bridge::RepositoriesRequest) -> Result<()> {
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
    use super::*;
    use fidl;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_remove() {
        let daemon = FakeDaemonBuilder::new().register_fidl_service::<Repo>().build();
        let proxy = daemon.open_proxy::<bridge::RepositoriesMarker>().await;
        let spec = bridge::RepositorySpec::Filesystem(bridge::FileSystemRepositorySpec {
            path: Some("test_path/bar".to_owned()),
            ..bridge::FileSystemRepositorySpec::EMPTY
        });
        proxy.add("mine", &mut spec.clone()).unwrap();

        let (client, server) = fidl::endpoints::create_endpoints().unwrap();
        proxy.list(server).unwrap();
        let client = client.into_proxy().unwrap();

        let next = client.next().await.unwrap();

        assert_eq!(1, next.len());
        assert_eq!(0, client.next().await.unwrap().len());

        let got = &next[0];
        assert_eq!("mine", &got.name);
        assert_eq!(spec, got.spec);

        assert!(proxy.remove("mine").await.unwrap());

        let (client, server) = fidl::endpoints::create_endpoints().unwrap();
        proxy.list(server).unwrap();
        let client = client.into_proxy().unwrap();

        assert_eq!(0, client.next().await.unwrap().len());
    }
}
