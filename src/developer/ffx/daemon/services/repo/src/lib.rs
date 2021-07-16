// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{anyhow, Context as _, Result},
    async_lock::{Mutex, RwLock},
    async_trait::async_trait,
    ffx_config::{self, ConfigLevel},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_bridge as bridge,
    fidl_fuchsia_developer_bridge_ext::{RepositorySpec, RepositoryTarget},
    fidl_fuchsia_pkg::RepositoryManagerMarker,
    fidl_fuchsia_pkg_rewrite::{EngineMarker, LiteralRule, Rule},
    fuchsia_async as fasync,
    futures::{FutureExt as _, StreamExt as _},
    itertools::Itertools as _,
    pkg::repository::{Repository, RepositoryManager, RepositoryServer, LISTEN_PORT},
    serde_json::{self, Value},
    services::prelude::*,
    std::{
        collections::HashMap,
        convert::{TryFrom, TryInto},
        net,
        sync::Arc,
        time::Duration,
    },
};

const REPOSITORY_MANAGER_SELECTOR: &str = "core/appmgr:out:fuchsia.pkg.RepositoryManager";
const REWRITE_SERVICE_SELECTOR: &str = "core/appmgr:out:fuchsia.pkg.rewrite.Engine";
const SHUTDOWN_TIMEOUT: Duration = Duration::from_secs(5);
const MAX_PACKAGES: i64 = 512;
const MAX_REGISTERED_TARGETS: i64 = 512;

struct ServerInfo {
    server: RepositoryServer,
    addr: net::SocketAddr,
    task: fasync::Task<()>,
}

#[ffx_service]
pub struct Repo {
    manager: Arc<RepositoryManager>,
    server: RwLock<Option<ServerInfo>>,
    registered_targets: Mutex<HashMap<(String, String), RepositoryTarget>>,
}

#[derive(PartialEq)]
enum SaveConfig {
    Save,
    DoNotSave,
}

impl Repo {
    async fn start_server(&self, addr: net::SocketAddr) -> Result<()> {
        log::info!("Starting repository server on {}", addr);

        // FIXME(http://fxbug.dev/77146) We currently can only run on 127.0.0.1:8085 and [::1]:8085.
        let required_addr = (net::Ipv6Addr::LOCALHOST, LISTEN_PORT).into();
        if addr != required_addr {
            return Err(anyhow!(
                "repository currently only supports {}, not {}",
                required_addr,
                addr
            ));
        }

        let mut server_locked = self.server.write().await;

        // Exit early if we're already running on this address.
        if let Some(server) = &*server_locked {
            if server.addr == addr {
                return Ok(());
            }
        }

        let (server_fut, server) = RepositoryServer::builder(addr, Arc::clone(&self.manager))
            .start()
            .await
            .context("starting repository server")?;

        log::info!("Started repository server on {}", server.local_addr());

        // Spawn the server future in the background to process requests from clients.
        let task = fasync::Task::local(server_fut);

        *server_locked = Some(ServerInfo { server, addr, task });

        Ok(())
    }

    async fn load_repositories_from_config(&self) {
        let value = match ffx_config::get::<Value, _>("repository.server.repositories").await {
            Ok(value) => value,
            Err(err) => {
                log::warn!("failed to load repositories: {:?}", err);
                return;
            }
        };

        let repos = match value {
            Value::Object(repos) => repos,
            _ => {
                log::warn!("expected repository.server.repositories to be a map, not {}", value);
                return;
            }
        };

        for (name, entry) in repos.into_iter() {
            // Parse the repository spec.
            let repo_spec = match serde_json::from_value(entry) {
                Ok(repo_spec) => repo_spec,
                Err(err) => {
                    log::warn!("failed to parse repository {:?}: {:?}", name, err);
                    continue;
                }
            };

            // Add the repository.
            if let Err(err) = self.add_repository(&name, repo_spec, SaveConfig::DoNotSave).await {
                log::warn!("failed to add the repository {:?}: {:?}", name, err);
                continue;
            }
        }
    }

    async fn load_registrations_from_config(&self, cx: &Context) {
        let value = match ffx_config::get::<Value, _>("repository.server.registrations").await {
            Ok(value) => value,
            Err(err) => {
                log::warn!("failed to load registrations: {:?}", err);
                return;
            }
        };

        let registrations = match value {
            Value::Object(registrations) => registrations,
            _ => {
                log::warn!("expected repository.server.registrations to be a map, not {}", value);
                return;
            }
        };

        for (repo_name, targets) in registrations.into_iter() {
            let targets = match targets {
                Value::Object(targets) => targets,
                _ => {
                    log::warn!(
                        "repository {:?} targets should be a map, not {}",
                        repo_name,
                        targets
                    );
                    continue;
                }
            };

            for (target_nodename, target_info) in targets.into_iter() {
                let target_info = match serde_json::from_value(target_info) {
                    Ok(target_info) => target_info,
                    Err(err) => {
                        log::warn!(
                            "failed to parse registration {:?} {:?}: {:?}",
                            repo_name,
                            target_nodename,
                            err
                        );
                        continue;
                    }
                };

                if let Err(err) = self.register_target(cx, target_info, SaveConfig::DoNotSave).await
                {
                    log::warn!(
                        "failed to register target {:?} {:?}: {:?}",
                        repo_name,
                        target_nodename,
                        err
                    );
                    continue;
                }
            }
        }
    }

    async fn add_repository(
        &self,
        name: &str,
        repo_spec: RepositorySpec,
        save_config: SaveConfig,
    ) -> std::result::Result<(), bridge::RepositoryError> {
        log::info!("Adding repository {} {:?}", name, repo_spec);

        let json_repo_spec = serde_json::to_value(repo_spec.clone()).map_err(|err| {
            log::error!("Unable to serialize repository spec {:?}: {:#?}", repo_spec, err);
            bridge::RepositoryError::InternalError
        })?;

        // Create the repository.
        let repo = Repository::from_repository_spec(name, repo_spec).await.map_err(|err| {
            log::error!("Unable to create repository: {:#?}", err);
            bridge::RepositoryError::IoError
        })?;

        if save_config == SaveConfig::Save {
            // Save the filesystem configuration.
            ffx_config::set(
                (format!("repository.server.repositories.{}", name).as_str(), ConfigLevel::User),
                json_repo_spec,
            )
            .await
            .map_err(|err| {
                log::error!("Failed to save repository: {:#?}", err);
                bridge::RepositoryError::IoError
            })?;
        }

        // Finally add the repository.
        self.manager.add(Arc::new(repo));

        Ok(())
    }

    async fn register_target(
        &self,
        cx: &Context,
        target_info: RepositoryTarget,
        save_config: SaveConfig,
    ) -> std::result::Result<(), bridge::RepositoryError> {
        log::info!(
            "Registering repository {:?} for target {:?}",
            target_info.repo_name,
            target_info.target_identifier
        );

        let json_target_info = serde_json::to_value(&target_info).map_err(|err| {
            log::error!("Unable to serialize registration info {:?}: {:#?}", target_info, err);
            bridge::RepositoryError::InternalError
        })?;

        let repo = self
            .manager
            .get(&target_info.repo_name)
            .ok_or_else(|| bridge::RepositoryError::NoMatchingRepository)?;

        let (target, proxy) = cx
            .open_target_proxy_with_info::<RepositoryManagerMarker>(
                target_info.target_identifier.clone(),
                REPOSITORY_MANAGER_SELECTOR,
            )
            .await
            .map_err(|err| {
                log::warn!(
                    "Failed to open target proxy with target name {:?}: {:#?}",
                    target_info.target_identifier,
                    err
                );
                bridge::RepositoryError::TargetCommunicationFailure
            })?;

        let target_nodename = target.nodename.ok_or_else(|| {
            log::warn!("target {:?} does not have a nodename", target_info.target_identifier);
            bridge::RepositoryError::InternalError
        })?;

        // TODO(fxbug.dev/77015): parameterize the mirror_url value here once we are dynamically assigning ports.
        let config = repo
            .get_config(
                &format!("localhost:{}/{}", LISTEN_PORT, repo.name()),
                target_info.storage_type.clone().map(|storage_type| storage_type.into()),
            )
            .await
            .map_err(|e| {
                log::warn!("failed to get config: {}", e);
                return bridge::RepositoryError::RepositoryManagerError;
            })?;

        match proxy.add(config.into()).await {
            Ok(Ok(())) => {}
            Ok(Err(err)) => {
                log::warn!("Failed to add config: {:#?}", err);
                return Err(bridge::RepositoryError::RepositoryManagerError);
            }
            Err(err) => {
                log::warn!("Failed to add config: {:#?}", err);
                return Err(bridge::RepositoryError::TargetCommunicationFailure);
            }
        }

        if !target_info.aliases.is_empty() {
            let rewrite_proxy = match cx
                .open_target_proxy::<EngineMarker>(
                    target_info.target_identifier.clone(),
                    REWRITE_SERVICE_SELECTOR,
                )
                .await
            {
                Ok(p) => p,
                Err(err) => {
                    log::warn!(
                        "Failed to open Rewrite Engine target proxy with target name {:?}: {:#?}",
                        target_info.target_identifier,
                        err
                    );
                    return Err(bridge::RepositoryError::TargetCommunicationFailure);
                }
            };

            let (transaction_proxy, server_end) = create_proxy().map_err(|err| {
                log::warn!("Failed to create Rewrite transaction: {:#?}", err);
                bridge::RepositoryError::RewriteEngineError
            })?;

            rewrite_proxy.start_edit_transaction(server_end).map_err(|err| {
                log::warn!("Failed to start edit transaction: {:#?}", err);
                bridge::RepositoryError::RewriteEngineError
            })?;

            for alias in target_info.aliases.iter() {
                let rule = LiteralRule {
                    host_match: alias.to_string(),
                    host_replacement: repo.name().to_string(),
                    path_prefix_match: "/".to_string(),
                    path_prefix_replacement: "/".to_string(),
                };
                match transaction_proxy.add(&mut Rule::Literal(rule)).await {
                    Err(err) => {
                        log::warn!("Failed to add rewrite rule. Error was: {:#?}", err);
                        return Err(bridge::RepositoryError::RewriteEngineError);
                    }
                    Ok(Err(err)) => {
                        log::warn!("Adding rewrite rule returned failure. Error was: {:#?}", err);
                        return Err(bridge::RepositoryError::RewriteEngineError);
                    }
                    Ok(_) => {}
                }
            }

            match transaction_proxy.commit().await {
                Ok(Ok(())) => {}
                Ok(Err(err)) => {
                    log::warn!("Committing rewrite rule returned failure. Error was: {:#?}", err);
                    return Err(bridge::RepositoryError::RewriteEngineError);
                }
                Err(err) => {
                    log::warn!("Failed to commit rewrite rule. Error was: {:#?}", err);
                    return Err(bridge::RepositoryError::RewriteEngineError);
                }
            }
        }

        if save_config == SaveConfig::Save {
            ffx_config::set(
                (
                    format!("repository.server.registrations.{}.{}", repo.name(), target_nodename)
                        .as_str(),
                    ConfigLevel::User,
                ),
                json_target_info,
            )
            .await
            .map_err(|err| {
                log::warn!("Failed to save registration to config: {:#?}", err);
                bridge::RepositoryError::InternalError
            })?;
        }

        self.registered_targets
            .lock()
            .await
            .insert((target_info.repo_name.clone(), target_nodename), target_info);

        Ok(())
    }

    async fn deregister_target(
        &self,
        cx: &Context,
        repo_name: String,
        target_identifier: Option<String>,
    ) -> std::result::Result<(), bridge::RepositoryError> {
        log::info!("Deregistering repository {:?} from target {:?}", repo_name, target_identifier);

        let repo = self
            .manager
            .get(&repo_name)
            .ok_or_else(|| bridge::RepositoryError::NoMatchingRepository)?;

        // Hold the lock during the unregistration process.
        let mut registered_targets = self.registered_targets.lock().await;

        let (target, proxy) = cx
            .open_target_proxy_with_info::<RepositoryManagerMarker>(
                target_identifier.clone(),
                REPOSITORY_MANAGER_SELECTOR,
            )
            .await
            .map_err(|err| {
                log::warn!(
                    "Failed to open target proxy with target name {:?}: {:#?}",
                    target_identifier,
                    err
                );
                bridge::RepositoryError::TargetCommunicationFailure
            })?;

        let target_nodename = target.nodename.ok_or_else(|| {
            log::warn!("target {:?} does not have a nodename", target_identifier);
            bridge::RepositoryError::InternalError
        })?;

        match proxy.remove(&repo.repo_url()).await {
            Ok(Ok(())) => {}
            Ok(Err(err)) => {
                log::warn!("failed to deregister repo {:?}: {:?}", repo_name, err);
                return Err(bridge::RepositoryError::InternalError);
            }
            Err(err) => {
                log::warn!("Failed to remove repo: {:?}", err);
                return Err(bridge::RepositoryError::TargetCommunicationFailure);
            }
        }

        ffx_config::remove((
            format!("repository.server.registrations.{}.{}", repo_name, target_nodename,).as_str(),
            ConfigLevel::User,
        ))
        .await
        .map_err(|err| {
            log::warn!("Failed to remove registration from config: {:#?}", err);
            bridge::RepositoryError::InternalError
        })?;

        registered_targets.remove(&(repo_name, target_nodename));

        Ok(())
    }
}

impl Default for Repo {
    fn default() -> Self {
        Repo {
            manager: RepositoryManager::new(),
            server: RwLock::new(None),
            registered_targets: Mutex::new(HashMap::new()),
        }
    }
}

#[async_trait(?Send)]
impl FidlService for Repo {
    type Service = bridge::RepositoryRegistryMarker;
    type StreamHandler = FidlStreamHandler<Self>;

    async fn handle(&self, cx: &Context, req: bridge::RepositoryRegistryRequest) -> Result<()> {
        match req {
            bridge::RepositoryRegistryRequest::AddRepository { name, repository, responder } => {
                let mut res = match repository.try_into() {
                    Ok(repo_spec) => self.add_repository(&name, repo_spec, SaveConfig::Save).await,
                    Err(err) => Err(err.into()),
                };

                responder.send(&mut res)?;
                Ok(())
            }
            bridge::RepositoryRegistryRequest::RemoveRepository { name, responder } => {
                log::info!("Removing repository {}", name);

                responder.send(self.manager.remove(&name))?;
                Ok(())
            }
            bridge::RepositoryRegistryRequest::RegisterTarget { target_info, responder } => {
                let mut res = match RepositoryTarget::try_from(target_info) {
                    Ok(target_info) => {
                        self.register_target(cx, target_info, SaveConfig::Save).await
                    }
                    Err(err) => Err(err.into()),
                };

                responder.send(&mut res)?;
                Ok(())
            }
            bridge::RepositoryRegistryRequest::DeregisterTarget {
                repository_name,
                target_identifier,
                responder,
            } => {
                responder.send(
                    &mut self.deregister_target(cx, repository_name, target_identifier).await,
                )?;
                Ok(())
            }
            bridge::RepositoryRegistryRequest::ListPackages { name, iterator, responder } => {
                let mut stream = match iterator.into_stream() {
                    Ok(s) => s,
                    Err(e) => {
                        log::warn!("error converting iterator to stream: {}", e);
                        responder.send(&mut Err(bridge::RepositoryError::InternalError))?;
                        return Ok(());
                    }
                };
                let repo = if let Some(r) = self.manager.get(&name) {
                    r
                } else {
                    responder.send(&mut Err(bridge::RepositoryError::NoMatchingRepository))?;
                    return Ok(());
                };

                let values = repo.list_packages().await?;

                let mut pos = 0;

                fasync::Task::spawn(async move {
                    while let Some(request) = stream.next().await {
                        match request {
                            Ok(bridge::RepositoryPackagesIteratorRequest::Next { responder }) => {
                                let len = values.len();
                                let chunk = &mut values[pos..]
                                    [..std::cmp::min(len - pos, MAX_PACKAGES as usize)]
                                    .into_iter()
                                    .map(|p| p.clone());
                                pos += MAX_PACKAGES as usize;
                                pos = std::cmp::min(pos, len);

                                if let Err(e) = responder.send(chunk) {
                                    log::warn!(
                                        "Error responding to RepositoryPackagesIterator request: {}",
                                        e
                                    );
                                }
                            }
                            Err(e) => {
                                log::warn!("Error in RepositoryPackagesIterator request stream: {}", e)
                            }
                        }
                    }
                })
                .detach();

                responder.send(&mut Ok(()))?;
                Ok(())
            }
            bridge::RepositoryRegistryRequest::ListRepositories { iterator, .. } => {
                let mut stream = iterator.into_stream()?;
                let mut values = self
                    .manager
                    .repositories()
                    .map(|x| bridge::RepositoryConfig {
                        name: x.name().to_owned(),
                        spec: x.spec().into(),
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

                                if let Err(err) = responder.send(chunk) {
                                    log::warn!(
                                        "Error responding to RepositoryIterator request: {:#?}",
                                        err
                                    );
                                }
                            }
                            Err(err) => {
                                log::warn!("Error in RepositoryIterator request stream: {:#?}", err)
                            }
                        }
                    }
                })
                .detach();
                Ok(())
            }
            bridge::RepositoryRegistryRequest::ListRegisteredTargets { iterator, .. } => {
                let mut stream = iterator.into_stream()?;
                let mut values = self
                    .registered_targets
                    .lock()
                    .await
                    .values()
                    .cloned()
                    .map(|x| x.into())
                    .chunks(MAX_REGISTERED_TARGETS as usize)
                    .into_iter()
                    .map(|chunk| chunk.collect::<Vec<_>>())
                    .collect::<Vec<_>>()
                    .into_iter();

                fasync::Task::spawn(async move {
                    while let Some(request) = stream.next().await {
                        match request {
                            Ok(bridge::RepositoryTargetsIteratorRequest::Next { responder }) => {
                                if let Err(err) = responder.send(&mut values.next().unwrap_or_else(Vec::new).into_iter()) {
                                    log::warn!(
                                        "Error responding to RepositoryTargetsIterator request: {:#?}",
                                        err
                                    );
                                }
                            }
                            Err(err) => {
                                log::warn!("Error in RepositoryTargetsIterator request stream: {:#?}", err)
                            }
                        }
                    }
                })
                .detach();
                Ok(())
            }
        }
    }

    async fn start(&mut self, cx: &Context) -> Result<()> {
        log::info!("Starting repository service");

        self.load_repositories_from_config().await;
        self.load_registrations_from_config(cx).await;

        match ffx_config::get::<String, _>("repository.server.listen").await {
            Ok(address) => {
                if let Ok(address) = address.parse::<net::SocketAddr>() {
                    if let Err(err) = self.start_server(address).await {
                        log::warn!("Failed to start repository server: {:#?}", err);
                    }
                } else {
                    log::warn!("Invalid value for repository.server.listen")
                }
            }
            Err(err) => {
                log::warn!("Failed to read server address from config: {:#?}", err);
            }
        }

        Ok(())
    }

    async fn stop(&mut self, _cx: &Context) -> Result<()> {
        log::info!("Stopping repository service");

        let server_info = self.server.write().await.take();
        if let Some(server_info) = server_info {
            server_info.server.stop();

            futures::select! {
                () = server_info.task.fuse() => {},
                () = fasync::Timer::new(SHUTDOWN_TIMEOUT).fuse() => {
                    log::error!("Timed out waiting for the repository server to shut down");
                },
            }
        }

        // Drop all repositories.
        self.manager.clear();

        // Drop all registered targets.
        self.registered_targets.lock().await.clear();

        log::info!("Repository service has been stopped");

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl,
        fidl_fuchsia_pkg::{
            MirrorConfig, RepositoryConfig, RepositoryKeyConfig, RepositoryManagerRequest,
        },
        fidl_fuchsia_pkg_rewrite::{EditTransactionRequest, EngineMarker, EngineRequest},
        services::testing::{FakeDaemon, FakeDaemonBuilder},
        std::sync::{Arc, Mutex},
    };

    const REPO_NAME: &str = "some_repo";
    const TARGET_NODENAME: &str = "some_target";
    const EMPTY_REPO_PATH: &str = "host_x64/test_data/ffx_daemon_service_repo/empty-repo";

    fn test_repo_config() -> RepositoryConfig {
        RepositoryConfig {
            mirrors: Some(vec![MirrorConfig {
                mirror_url: Some(format!("http://localhost:8085/{}", REPO_NAME)),
                subscribe: Some(true),
                ..MirrorConfig::EMPTY
            }]),
            repo_url: Some(format!("fuchsia-pkg://{}", REPO_NAME)),
            root_keys: Some(vec![RepositoryKeyConfig::Ed25519Key(vec![
                29, 76, 86, 76, 184, 70, 108, 73, 249, 127, 4, 47, 95, 63, 36, 35, 101, 255, 212,
                33, 10, 154, 26, 130, 117, 157, 125, 88, 175, 214, 109, 113,
            ])]),
            root_version: Some(1),
            ..RepositoryConfig::EMPTY
        }
    }

    #[derive(Default)]
    struct FakeRepositoryManager;

    #[async_trait(?Send)]
    impl FidlService for FakeRepositoryManager {
        type Service = RepositoryManagerMarker;
        type StreamHandler = FidlStreamHandler<Self>;

        async fn handle(&self, _cx: &Context, req: RepositoryManagerRequest) -> Result<()> {
            match req {
                RepositoryManagerRequest::Add { repo, responder } => {
                    assert_eq!(repo, test_repo_config(),);
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
                    assert_eq!(repo, test_repo_config(),);

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

    async fn add_repo(daemon: &FakeDaemon) -> bridge::RepositoryRegistryProxy {
        let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;
        let spec = RepositorySpec::FileSystem { path: EMPTY_REPO_PATH.into() };
        proxy
            .add_repository(REPO_NAME, &mut spec.into())
            .await
            .expect("communicated with proxy")
            .expect("adding repository to succeed");

        proxy
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_remove() {
        ffx_config::init_config_test().unwrap();

        let daemon = FakeDaemonBuilder::new().register_fidl_service::<Repo>().build();

        let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;
        let spec = bridge::RepositorySpec::FileSystem(bridge::FileSystemRepositorySpec {
            path: Some(EMPTY_REPO_PATH.to_owned()),
            ..bridge::FileSystemRepositorySpec::EMPTY
        });
        proxy
            .add_repository(REPO_NAME, &mut spec.clone())
            .await
            .expect("communicated with proxy")
            .expect("adding repository to succeed");

        let (client, server) = fidl::endpoints::create_endpoints().unwrap();
        proxy.list_repositories(server).unwrap();
        let client = client.into_proxy().unwrap();

        let next = client.next().await.unwrap();

        assert_eq!(1, next.len());
        assert_eq!(0, client.next().await.unwrap().len());

        let got = &next[0];
        assert_eq!(REPO_NAME, &got.name);
        assert_eq!(spec, got.spec);

        assert!(proxy.remove_repository(REPO_NAME).await.unwrap());

        let (client, server) = fidl::endpoints::create_endpoints().unwrap();
        proxy.list_repositories(server).unwrap();
        let client = client.into_proxy().unwrap();

        assert_eq!(0, client.next().await.unwrap().len());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_register_deregister() {
        ffx_config::init_config_test().unwrap();

        #[derive(Debug, PartialEq)]
        enum Event {
            Add { repo: RepositoryConfig },
            Remove { repo_url: String },
        }
        let events = Arc::new(Mutex::new(Vec::new()));
        let events_closure = Arc::clone(&events);

        let daemon = FakeDaemonBuilder::new()
            .register_instanced_service_closure::<RepositoryManagerMarker, _>(move |_cx, req| {
                match req {
                    RepositoryManagerRequest::Add { repo, responder } => {
                        events_closure.lock().unwrap().push(Event::Add { repo });
                        responder.send(&mut Ok(()))?;
                        Ok(())
                    }
                    RepositoryManagerRequest::Remove { repo_url, responder } => {
                        events_closure.lock().unwrap().push(Event::Remove { repo_url });
                        responder.send(&mut Ok(()))?;
                        Ok(())
                    }
                    _ => panic!("unexpected request: {:?}", req),
                }
            })
            .register_fidl_service::<FakeEngine>()
            .register_fidl_service::<Repo>()
            .nodename(TARGET_NODENAME.to_string())
            .build();

        let proxy = add_repo(&daemon).await;

        let (client, server) = fidl::endpoints::create_endpoints().unwrap();
        proxy.list_registered_targets(server).unwrap();
        let client = client.into_proxy().unwrap();
        assert_eq!(0, client.next().await.unwrap().len());

        proxy
            .register_target(bridge::RepositoryTarget {
                repo_name: Some(REPO_NAME.to_string()),
                target_identifier: Some(TARGET_NODENAME.to_string()),
                aliases: Some(vec!["fuchsia.com".to_string()]),
                ..bridge::RepositoryTarget::EMPTY
            })
            .await
            .expect("communicated with proxy")
            .expect("target registration to succeed");

        assert_eq!(
            events.lock().unwrap().drain(..).collect::<Vec<_>>(),
            vec![Event::Add { repo: test_repo_config() }]
        );

        let (client, server) = fidl::endpoints::create_endpoints().unwrap();
        proxy.list_registered_targets(server).unwrap();
        let client = client.into_proxy().unwrap();
        let registered = client.next().await.unwrap();
        assert_eq!(0, client.next().await.unwrap().len());
        let mut registered = registered.into_iter();
        let (registered, end) = (registered.next().unwrap(), registered.next());
        assert!(end.is_none());
        assert_eq!(Some(TARGET_NODENAME.to_string()), registered.target_identifier);
        assert_eq!(Some(REPO_NAME.to_string()), registered.repo_name);

        proxy
            .deregister_target(REPO_NAME, Some(TARGET_NODENAME))
            .await
            .expect("communicated with proxy")
            .expect("target unregistration to succeed");

        assert_eq!(
            events.lock().unwrap().drain(..).collect::<Vec<_>>(),
            vec![Event::Remove { repo_url: format!("fuchsia-pkg://{}", REPO_NAME) }]
        );

        let (client, server) = fidl::endpoints::create_endpoints().unwrap();
        proxy.list_registered_targets(server).unwrap();
        let client = client.into_proxy().unwrap();
        assert_eq!(0, client.next().await.unwrap().len());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_register_default_target() {
        ffx_config::init_config_test().unwrap();

        #[derive(Debug, PartialEq)]
        enum Event {
            Add { repo: RepositoryConfig },
            Remove { repo_url: String },
        }
        let events = Arc::new(Mutex::new(Vec::new()));
        let events_closure = Arc::clone(&events);

        let daemon = FakeDaemonBuilder::new()
            .register_instanced_service_closure::<RepositoryManagerMarker, _>(move |_cx, req| {
                match req {
                    RepositoryManagerRequest::Add { repo, responder } => {
                        events_closure.lock().unwrap().push(Event::Add { repo });
                        responder.send(&mut Ok(()))?;
                        Ok(())
                    }
                    RepositoryManagerRequest::Remove { repo_url, responder } => {
                        events_closure.lock().unwrap().push(Event::Remove { repo_url });
                        responder.send(&mut Ok(()))?;
                        Ok(())
                    }
                    _ => panic!("unexpected request: {:?}", req),
                }
            })
            .register_fidl_service::<FakeEngine>()
            .register_fidl_service::<Repo>()
            .nodename(TARGET_NODENAME.to_string())
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
            .expect("communicated with proxy")
            .expect("target registration to succeed");

        assert_eq!(
            events.lock().unwrap().drain(..).collect::<Vec<_>>(),
            vec![Event::Add { repo: test_repo_config() }]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_register_empty_aliases() {
        ffx_config::init_config_test().unwrap();

        let daemon = FakeDaemonBuilder::new()
            .register_fidl_service::<FakeRepositoryManager>()
            .register_fidl_service::<FakeEngine>()
            .register_fidl_service::<Repo>()
            .nodename(TARGET_NODENAME.to_string())
            .build();

        let proxy = add_repo(&daemon).await;
        proxy
            .register_target(bridge::RepositoryTarget {
                repo_name: Some(REPO_NAME.to_string()),
                target_identifier: Some(TARGET_NODENAME.to_string()),
                aliases: Some(vec![]),
                ..bridge::RepositoryTarget::EMPTY
            })
            .await
            .unwrap()
            .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_register_none_aliases() {
        ffx_config::init_config_test().unwrap();

        let daemon = FakeDaemonBuilder::new()
            .register_fidl_service::<FakeRepositoryManager>()
            .register_fidl_service::<FakeEngine>()
            .register_fidl_service::<Repo>()
            .nodename(TARGET_NODENAME.to_string())
            .build();

        let proxy = add_repo(&daemon).await;
        proxy
            .register_target(bridge::RepositoryTarget {
                repo_name: Some(REPO_NAME.to_string()),
                target_identifier: Some(TARGET_NODENAME.to_string()),
                aliases: None,
                ..bridge::RepositoryTarget::EMPTY
            })
            .await
            .unwrap()
            .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_register_repo_manager_error() {
        ffx_config::init_config_test().unwrap();

        let daemon = FakeDaemonBuilder::new()
            .register_fidl_service::<ErroringRepositoryManager>()
            .register_fidl_service::<FakeEngine>()
            .register_fidl_service::<Repo>()
            .nodename(TARGET_NODENAME.to_string())
            .build();

        let proxy = add_repo(&daemon).await;
        assert_eq!(
            proxy
                .register_target(bridge::RepositoryTarget {
                    repo_name: Some(REPO_NAME.to_string()),
                    target_identifier: Some(TARGET_NODENAME.to_string()),
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
            .nodename(TARGET_NODENAME.to_string())
            .build();

        let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;
        assert_eq!(
            proxy
                .register_target(bridge::RepositoryTarget {
                    repo_name: Some(REPO_NAME.to_string()),
                    target_identifier: Some(TARGET_NODENAME.to_string()),
                    aliases: None,
                    ..bridge::RepositoryTarget::EMPTY
                })
                .await
                .unwrap()
                .unwrap_err(),
            bridge::RepositoryError::NoMatchingRepository
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_deregister_non_existent_repo() {
        let daemon = FakeDaemonBuilder::new()
            .register_fidl_service::<ErroringRepositoryManager>()
            .register_fidl_service::<FakeEngine>()
            .register_fidl_service::<Repo>()
            .nodename(TARGET_NODENAME.to_string())
            .build();

        let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;
        assert_eq!(
            proxy.deregister_target(REPO_NAME, Some(TARGET_NODENAME)).await.unwrap().unwrap_err(),
            bridge::RepositoryError::NoMatchingRepository
        );
    }
}
