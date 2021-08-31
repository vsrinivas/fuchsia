// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{Context as _, Result},
    async_lock::RwLock,
    async_trait::async_trait,
    ffx_daemon_core::events::{EventHandler, Status as EventStatus},
    ffx_daemon_events::{DaemonEvent, TargetEvent},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_developer_bridge as bridge,
    fidl_fuchsia_developer_bridge_ext::{RepositorySpec, RepositoryTarget},
    fidl_fuchsia_pkg::RepositoryManagerMarker,
    fidl_fuchsia_pkg_rewrite::EngineMarker,
    fidl_fuchsia_pkg_rewrite_ext::{do_transaction, Rule},
    fuchsia_async as fasync,
    fuchsia_zircon_status::Status,
    futures::{FutureExt as _, StreamExt as _},
    itertools::Itertools as _,
    pkg::repository::{self, listen_addr, Repository, RepositoryManager, RepositoryServer},
    services::prelude::*,
    std::{
        collections::HashSet,
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
pub struct Repo<T: EventHandlerProvider = RealEventHandlerProvider> {
    manager: Arc<RepositoryManager>,
    server: RwLock<Option<ServerInfo>>,
    event_handler_provider: T,
}

#[derive(PartialEq)]
enum SaveConfig {
    Save,
    DoNotSave,
}

#[async_trait::async_trait(?Send)]
pub trait EventHandlerProvider {
    async fn setup_event_handlers(&self, cx: Context, manager: Arc<RepositoryManager>);
}

#[derive(Default)]
pub struct RealEventHandlerProvider;

#[async_trait::async_trait(?Send)]
impl EventHandlerProvider for RealEventHandlerProvider {
    async fn setup_event_handlers(&self, cx: Context, manager: Arc<RepositoryManager>) {
        let q = cx.daemon_event_queue().await;
        q.add_handler(DaemonEventHandler { cx, manager }).await;
    }
}

async fn add_repository(
    repo_name: &str,
    repo_spec: RepositorySpec,
    save_config: SaveConfig,
    manager: Arc<RepositoryManager>,
) -> Result<(), bridge::RepositoryError> {
    log::info!("Adding repository {} {:?}", repo_name, repo_spec);

    // Create the repository.
    let repo =
        Repository::from_repository_spec(repo_name, repo_spec.clone()).await.map_err(|err| {
            log::error!("Unable to create repository: {:#?}", err);

            match err {
                repository::Error::Tuf(tuf::Error::ExpiredMetadata(_)) => {
                    bridge::RepositoryError::ExpiredRepositoryMetadata
                }
                _ => bridge::RepositoryError::IoError,
            }
        })?;

    if save_config == SaveConfig::Save {
        // Save the filesystem configuration.
        pkg::config::set_repository(repo_name, &repo_spec).await.map_err(|err| {
            log::error!("Failed to save repository: {:#?}", err);
            bridge::RepositoryError::IoError
        })?;
    }

    // Finally add the repository.
    manager.add(Arc::new(repo));

    Ok(())
}

async fn register_target(
    cx: &Context,
    target_info: RepositoryTarget,
    save_config: SaveConfig,
    manager: Arc<RepositoryManager>,
) -> Result<(), bridge::RepositoryError> {
    log::info!(
        "Registering repository {:?} for target {:?}",
        target_info.repo_name,
        target_info.target_identifier
    );

    let repo = manager
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

    let listen_addr =
        ffx_config::get::<String, _>("repository.server.listen").await.map_err(|e| {
            log::error!("failed to get listen addr from config: {:?}", e);
            bridge::RepositoryError::InternalError
        })?;
    let config = repo
        .get_config(
            &format!("{}/{}", listen_addr, repo.name()),
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
            log::warn!("Failed to add config: {:#?}", Status::from_raw(err));
            return Err(bridge::RepositoryError::RepositoryManagerError);
        }
        Err(err) => {
            log::warn!("Failed to add config: {:#?}", err);
            return Err(bridge::RepositoryError::TargetCommunicationFailure);
        }
    }

    if !target_info.aliases.is_empty() {
        let () = create_aliases(cx, repo.name(), &target_nodename, &target_info.aliases).await?;
    }

    if save_config == SaveConfig::Save {
        pkg::config::set_registration(&target_nodename, &target_info).await.map_err(|err| {
            log::warn!("Failed to save registration to config: {:#?}", err);
            bridge::RepositoryError::InternalError
        })?;
    }

    Ok(())
}

fn aliases_to_rules(
    repo_name: &str,
    aliases: &[String],
) -> Result<Vec<Rule>, bridge::RepositoryError> {
    let rules = aliases
        .iter()
        .map(|alias| {
            Rule::new(alias.to_string(), repo_name.to_string(), "/".to_string(), "/".to_string())
        })
        .collect::<Result<Vec<_>, _>>()
        .map_err(|err| {
            log::warn!("failed to construct rule: {:#?}", err);
            bridge::RepositoryError::RewriteEngineError
        })?;

    Ok(rules)
}

async fn create_aliases(
    cx: &Context,
    repo_name: &str,
    target_nodename: &str,
    aliases: &[String],
) -> Result<(), bridge::RepositoryError> {
    let alias_rules = aliases_to_rules(repo_name, &aliases)?;

    let rewrite_proxy = match cx
        .open_target_proxy::<EngineMarker>(
            Some(target_nodename.to_string()),
            REWRITE_SERVICE_SELECTOR,
        )
        .await
    {
        Ok(p) => p,
        Err(err) => {
            log::warn!(
                "Failed to open Rewrite Engine target proxy with target name {:?}: {:#?}",
                target_nodename,
                err
            );
            return Err(bridge::RepositoryError::TargetCommunicationFailure);
        }
    };

    do_transaction(&rewrite_proxy, |transaction| async {
        // Prepend the alias rules to the front so they take priority.
        let mut rules = alias_rules.iter().cloned().rev().collect::<Vec<_>>();
        rules.extend(transaction.list_dynamic().await?);

        // Clear the list, since we'll be adding it back later.
        transaction.reset_all()?;

        // Remove duplicated rules while preserving order.
        rules.dedup();

        // Add the rules back into the transaction. We do it in reverse, because `.add()`
        // always inserts rules into the front of the list.
        for rule in rules.into_iter().rev() {
            transaction.add(rule).await?
        }

        Ok(transaction)
    })
    .await
    .map_err(|err| {
        log::warn!("failed to create transactions: {:#?}", err);
        bridge::RepositoryError::RewriteEngineError
    })?;

    Ok(())
}

async fn remove_aliases(
    cx: &Context,
    repo_name: &str,
    target_nodename: &str,
    aliases: &[String],
) -> Result<(), bridge::RepositoryError> {
    let alias_rules = aliases_to_rules(repo_name, &aliases)?.into_iter().collect::<HashSet<_>>();

    let rewrite_proxy = match cx
        .open_target_proxy::<EngineMarker>(
            Some(target_nodename.to_string()),
            REWRITE_SERVICE_SELECTOR,
        )
        .await
    {
        Ok(p) => p,
        Err(err) => {
            log::warn!(
                "Failed to open Rewrite Engine target proxy with target name {:?}: {:#?}",
                target_nodename,
                err
            );
            return Err(bridge::RepositoryError::TargetCommunicationFailure);
        }
    };

    do_transaction(&rewrite_proxy, |transaction| async {
        // Prepend the alias rules to the front so they take priority.
        let mut rules = transaction.list_dynamic().await?;

        // Clear the list, since we'll be adding it back later.
        transaction.reset_all()?;

        // Remove duplicated rules while preserving order.
        rules.dedup();

        // Add the rules back into the transaction. We do it in reverse, because `.add()`
        // always inserts rules into the front of the list.
        for rule in rules.into_iter().rev() {
            // Only add the rule if it doesn't match our alias rule.
            if alias_rules.get(&rule).is_none() {
                transaction.add(rule).await?
            }
        }

        Ok(transaction)
    })
    .await
    .map_err(|err| {
        log::warn!("failed to create transactions: {:#?}", err);
        bridge::RepositoryError::RewriteEngineError
    })?;

    Ok(())
}

impl<T: EventHandlerProvider> Repo<T> {
    async fn start_server(&self, addr: net::SocketAddr) -> Result<(), anyhow::Error> {
        log::info!("Starting repository server on {}", addr);

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

    async fn remove_repository(&self, cx: &Context, repo_name: &str) -> bool {
        log::info!("Removing repository {:?}", repo_name);

        // First, remove any registrations for this repository.
        for (target_nodename, _) in pkg::config::get_repository_registrations(repo_name).await {
            match self
                .deregister_target(cx, repo_name.to_string(), Some(target_nodename.to_string()))
                .await
            {
                Ok(()) => {}
                Err(err) => {
                    log::warn!(
                        "failed to deregister repository {:?} from target {:?}: {:#?}",
                        repo_name,
                        target_nodename,
                        err
                    );
                }
            }
        }

        // If we are removing the default repository, make sure to remove it from the configuration
        // as well.
        match pkg::config::get_default_repository().await {
            Ok(Some(default_repo_name)) if repo_name == default_repo_name => {
                if let Err(err) = pkg::config::unset_default_repository().await {
                    log::warn!("failed to remove default repository: {:#?}", err);
                }
            }
            Ok(_) => {}
            Err(err) => {
                log::warn!("failed to determine default repository name: {:#?}", err);
            }
        }

        if let Err(err) = pkg::config::remove_repository(repo_name).await {
            log::warn!("failed to remove repository from config: {:#?}", err);
        }

        // Finally, stop serving the repository.
        self.manager.remove(repo_name)
    }

    /// Deregister the repository from the target.
    ///
    /// This only works for repositories managed by `ffx`. If the repository named `repo_name` is
    /// unknown to this service, error out rather than trying to remove the registration.
    async fn deregister_target(
        &self,
        cx: &Context,
        repo_name: String,
        target_identifier: Option<String>,
    ) -> Result<(), bridge::RepositoryError> {
        log::info!("Deregistering repository {:?} from target {:?}", repo_name, target_identifier);

        let repo = self
            .manager
            .get(&repo_name)
            .ok_or_else(|| bridge::RepositoryError::NoMatchingRepository)?;

        // Connect to the target. Error out if we can't connect, since we can't remove the registration
        // from it.
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
            log::warn!("Target {:?} does not have a nodename", target_identifier);
            bridge::RepositoryError::InternalError
        })?;

        // Look up the the registration info. Error out if we don't have any registrations for this
        // device.
        let registration_info = pkg::config::get_registration(&repo_name, &target_nodename)
            .await
            .map_err(|err| {
                log::warn!(
                    "Failed to find registration info for repo {:?} and target {:?}: {:#?}",
                    repo_name,
                    target_nodename,
                    err
                );
                bridge::RepositoryError::InternalError
            })?
            .ok_or_else(|| bridge::RepositoryError::NoMatchingRegistration)?;

        // Check if we created any aliases. If so, remove them from the device before removing the
        // repository.
        if !registration_info.aliases.is_empty() {
            let () = remove_aliases(cx, repo.name(), &target_nodename, &registration_info.aliases)
                .await?;
        }

        // Remove the repository from the device.
        match proxy.remove(&repo.repo_url()).await {
            Ok(Ok(())) => {}
            Ok(Err(err)) => {
                log::warn!(
                    "failed to deregister repo {:?}: {:?}",
                    repo_name,
                    Status::from_raw(err)
                );
                return Err(bridge::RepositoryError::InternalError);
            }
            Err(err) => {
                log::warn!("Failed to remove repo: {:?}", err);
                return Err(bridge::RepositoryError::TargetCommunicationFailure);
            }
        }

        // Finally, remove the registration config from the ffx config.
        pkg::config::remove_registration(&repo_name, &target_nodename).await.map_err(|err| {
            log::warn!("Failed to remove registration from config: {:#?}", err);
            bridge::RepositoryError::InternalError
        })?;

        Ok(())
    }

    async fn list_packages(
        &self,
        name: &str,
        iterator: ServerEnd<bridge::RepositoryPackagesIteratorMarker>,
        include_fields: bridge::ListFields,
    ) -> Result<(), bridge::RepositoryError> {
        let mut stream = match iterator.into_stream() {
            Ok(s) => s,
            Err(e) => {
                log::warn!("error converting iterator to stream: {}", e);
                return Err(bridge::RepositoryError::InternalError);
            }
        };

        let repo = if let Some(r) = self.manager.get(&name) {
            r
        } else {
            return Err(bridge::RepositoryError::NoMatchingRepository);
        };

        let values = repo.list_packages(include_fields).await.map_err(|err| {
            log::error!("Unable to list packages: {:#?}", err);

            match err {
                repository::Error::Tuf(tuf::Error::ExpiredMetadata(_)) => {
                    bridge::RepositoryError::ExpiredRepositoryMetadata
                }
                _ => bridge::RepositoryError::IoError,
            }
        })?;

        fasync::Task::spawn(async move {
            let mut pos = 0;
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

        Ok(())
    }

    async fn show_package(
        &self,
        repository_name: &str,
        package_name: &str,
        iterator: ServerEnd<bridge::PackageEntryIteratorMarker>,
    ) -> Result<(), bridge::RepositoryError> {
        let mut stream = match iterator.into_stream() {
            Ok(s) => s,
            Err(e) => {
                log::warn!("error converting iterator to stream: {}", e);
                return Err(bridge::RepositoryError::InternalError);
            }
        };

        let repo = if let Some(r) = self.manager.get(&repository_name) {
            r
        } else {
            return Err(bridge::RepositoryError::NoMatchingRepository);
        };

        let values = repo.show_package(package_name.to_owned()).await.map_err(|err| {
            log::error!("Unable to list package contents: {:#?}", err);
            bridge::RepositoryError::IoError
        })?;
        if values.is_none() {
            return Err(bridge::RepositoryError::TargetCommunicationFailure);
        }
        let values = values.unwrap();

        fasync::Task::spawn(async move {
            let mut pos = 0;
            while let Some(request) = stream.next().await {
                match request {
                    Ok(bridge::PackageEntryIteratorRequest::Next { responder }) => {
                        let len = values.len();
                        let chunk = &mut values[pos..]
                            [..std::cmp::min(len - pos, MAX_PACKAGES as usize)]
                            .into_iter()
                            .map(|p| p.clone());
                        pos += MAX_PACKAGES as usize;
                        pos = std::cmp::min(pos, len);

                        if let Err(e) = responder.send(chunk) {
                            log::warn!(
                                "Error responding to PackageEntryIteratorRequest request: {}",
                                e
                            );
                        }
                    }
                    Err(e) => {
                        log::warn!("Error in PackageEntryIteratorRequest request stream: {}", e)
                    }
                }
            }
        })
        .detach();

        Ok(())
    }
}

impl<T: EventHandlerProvider + Default> Default for Repo<T> {
    fn default() -> Self {
        Repo {
            manager: RepositoryManager::new(),
            server: RwLock::new(None),
            event_handler_provider: T::default(),
        }
    }
}

#[async_trait(?Send)]
impl<T: EventHandlerProvider + Default + Unpin + 'static> FidlService for Repo<T> {
    type Service = bridge::RepositoryRegistryMarker;
    type StreamHandler = FidlStreamHandler<Self>;

    async fn handle(
        &self,
        cx: &Context,
        req: bridge::RepositoryRegistryRequest,
    ) -> Result<(), anyhow::Error> {
        match req {
            bridge::RepositoryRegistryRequest::AddRepository { name, repository, responder } => {
                let mut res = match repository.try_into() {
                    Ok(repo_spec) => {
                        add_repository(
                            &name,
                            repo_spec,
                            SaveConfig::Save,
                            Arc::clone(&self.manager),
                        )
                        .await
                    }
                    Err(err) => Err(err.into()),
                };

                responder.send(&mut res)?;
                Ok(())
            }
            bridge::RepositoryRegistryRequest::RemoveRepository { name, responder } => {
                responder.send(self.remove_repository(cx, &name).await)?;
                Ok(())
            }
            bridge::RepositoryRegistryRequest::RegisterTarget { target_info, responder } => {
                let mut res = match RepositoryTarget::try_from(target_info) {
                    Ok(target_info) => {
                        register_target(
                            cx,
                            target_info,
                            SaveConfig::Save,
                            Arc::clone(&self.manager),
                        )
                        .await
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
            bridge::RepositoryRegistryRequest::ListPackages {
                name,
                iterator,
                include_fields,
                responder,
            } => {
                responder.send(&mut self.list_packages(&name, iterator, include_fields).await)?;
                Ok(())
            }
            bridge::RepositoryRegistryRequest::ShowPackage {
                repository_name,
                package_name,
                iterator,
                responder,
            } => {
                responder.send(
                    &mut self.show_package(&repository_name, &package_name, iterator).await,
                )?;
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
                let mut values = pkg::config::get_registrations()
                    .await
                    .into_values()
                    .map(|targets| targets.into_values())
                    .flatten()
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

    async fn start(&mut self, cx: &Context) -> Result<(), anyhow::Error> {
        log::info!("Starting repository service");

        load_repositories_from_config(&self.manager).await;

        self.event_handler_provider
            .setup_event_handlers(cx.clone(), Arc::clone(&self.manager))
            .await;

        match listen_addr().await {
            Ok(Some(addr)) => {
                if let Err(err) = self.start_server(addr).await {
                    log::warn!("Failed to start repository server: {:#?}", err);
                }
            }
            Ok(None) => {
                log::warn!("repository.server.listen_addr not configured, not starting server");
            }
            Err(err) => {
                log::warn!("Failed to read server address from config: {:#?}", err);
            }
        }

        Ok(())
    }

    async fn stop(&mut self, _cx: &Context) -> Result<(), anyhow::Error> {
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

        log::info!("Repository service has been stopped");

        Ok(())
    }
}

async fn load_repositories_from_config(manager: &Arc<RepositoryManager>) {
    for (name, repo_spec) in pkg::config::get_repositories().await {
        if manager.get(&name).is_some() {
            continue;
        }

        // Add the repository.
        if let Err(err) =
            add_repository(&name, repo_spec, SaveConfig::DoNotSave, Arc::clone(&manager)).await
        {
            log::warn!("failed to add the repository {:?}: {:?}", name, err);
            continue;
        }
    }
}

#[derive(Clone)]
struct DaemonEventHandler {
    cx: Context,
    manager: Arc<RepositoryManager>,
}

#[async_trait(?Send)]
impl EventHandler<DaemonEvent> for DaemonEventHandler {
    async fn on_event(&self, event: DaemonEvent) -> Result<EventStatus> {
        match event {
            DaemonEvent::NewTarget(t) => {
                if let Some(n) = t.nodename.as_ref() {
                    let (_, q) = self.cx.get_target_event_queue(Some(n.clone())).await?;
                    q.add_handler(TargetEventHandler::new(
                        self.cx.clone(),
                        Arc::clone(&self.manager),
                        n.clone(),
                    ))
                    .await;
                }
            }
            _ => {}
        }
        Ok(EventStatus::Waiting)
    }
}

#[derive(Clone)]
struct TargetEventHandler {
    cx: Context,
    manager: Arc<RepositoryManager>,
    target_identifier: String,
}

impl TargetEventHandler {
    fn new(cx: Context, manager: Arc<RepositoryManager>, target_identifier: String) -> Self {
        Self { cx, manager, target_identifier }
    }
}

#[async_trait(?Send)]
impl EventHandler<TargetEvent> for TargetEventHandler {
    async fn on_event(&self, event: TargetEvent) -> Result<EventStatus> {
        if !matches!(event, TargetEvent::RcsActivated) {
            return Ok(EventStatus::Waiting);
        }

        // Make sure we pick up any repositories that have been added since the last event.
        load_repositories_from_config(&self.manager).await;

        // Find any saved registrations for this target and register them on the device.
        for (repo_name, targets) in pkg::config::get_registrations().await {
            for (target_nodename, target_info) in targets {
                if target_nodename != self.target_identifier {
                    continue;
                }

                if let Err(err) = register_target(
                    &self.cx,
                    target_info,
                    SaveConfig::DoNotSave,
                    Arc::clone(&self.manager),
                )
                .await
                {
                    log::warn!(
                        "failed to register target {:?} {:?}: {:?}",
                        repo_name,
                        target_nodename,
                        err
                    );
                    continue;
                } else {
                    log::info!(
                        "successfully registered repository {:?} on target {:?}",
                        repo_name,
                        target_nodename,
                    );
                }
            }
        }

        Ok(EventStatus::Waiting)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        ffx_config::ConfigLevel,
        fidl::{self, endpoints::Request},
        fidl_fuchsia_developer_bridge_ext::RepositoryStorageType,
        fidl_fuchsia_pkg::{
            MirrorConfig, RepositoryConfig, RepositoryKeyConfig, RepositoryManagerRequest,
        },
        fidl_fuchsia_pkg_rewrite::{
            EditTransactionRequest, EngineMarker, EngineRequest, RuleIteratorRequest,
        },
        futures::TryStreamExt,
        matches::assert_matches,
        services::testing::FakeDaemonBuilder,
        std::{
            fs,
            future::Future,
            sync::{Arc, Mutex},
        },
    };

    const REPO_NAME: &str = "some-repo";
    const TARGET_NODENAME: &str = "some-target";
    const EMPTY_REPO_PATH: &str = "host_x64/test_data/ffx_daemon_service_repo/empty-repo";

    macro_rules! rule {
        ($host_match:expr => $host_replacement:expr,
         $path_prefix_match:expr => $path_prefix_replacement:expr) => {
            Rule::new($host_match, $host_replacement, $path_prefix_match, $path_prefix_replacement)
                .unwrap()
        };
    }

    fn test_repo_config() -> RepositoryConfig {
        RepositoryConfig {
            mirrors: Some(vec![MirrorConfig {
                mirror_url: Some(format!("http://[::1]:8085/{}", REPO_NAME)),
                subscribe: Some(true),
                ..MirrorConfig::EMPTY
            }]),
            repo_url: Some(format!("fuchsia-pkg://{}", REPO_NAME)),
            root_keys: Some(vec![RepositoryKeyConfig::Ed25519Key(vec![
                29, 76, 86, 76, 184, 70, 108, 73, 249, 127, 4, 47, 95, 63, 36, 35, 101, 255, 212,
                33, 10, 154, 26, 130, 117, 157, 125, 88, 175, 214, 109, 113,
            ])]),
            root_version: Some(1),
            storage_type: Some(fidl_fuchsia_pkg::RepositoryStorageType::Ephemeral),
            ..RepositoryConfig::EMPTY
        }
    }

    struct FakeRepositoryManager {
        events: Arc<Mutex<Vec<RepositoryManagerEvent>>>,
    }

    impl FakeRepositoryManager {
        fn new() -> (
            Self,
            impl Fn(&Context, Request<RepositoryManagerMarker>) -> Result<(), anyhow::Error> + 'static,
        ) {
            let events = Arc::new(Mutex::new(Vec::new()));
            let events_closure = Arc::clone(&events);

            let closure = move |_cx: &Context, req| match req {
                RepositoryManagerRequest::Add { repo, responder } => {
                    events_closure.lock().unwrap().push(RepositoryManagerEvent::Add { repo });
                    responder.send(&mut Ok(()))?;
                    Ok(())
                }
                RepositoryManagerRequest::Remove { repo_url, responder } => {
                    events_closure
                        .lock()
                        .unwrap()
                        .push(RepositoryManagerEvent::Remove { repo_url });
                    responder.send(&mut Ok(()))?;
                    Ok(())
                }
                _ => panic!("unexpected request: {:?}", req),
            };

            (Self { events }, closure)
        }

        fn take_events(&self) -> Vec<RepositoryManagerEvent> {
            self.events.lock().unwrap().drain(..).collect::<Vec<_>>()
        }
    }

    #[derive(Debug, PartialEq)]
    enum RepositoryManagerEvent {
        Add { repo: RepositoryConfig },
        Remove { repo_url: String },
    }

    #[derive(Default)]
    struct ErroringRepositoryManager {}

    #[async_trait(?Send)]
    impl FidlService for ErroringRepositoryManager {
        type Service = RepositoryManagerMarker;
        type StreamHandler = FidlStreamHandler<Self>;

        async fn handle(
            &self,
            _cx: &Context,
            req: RepositoryManagerRequest,
        ) -> Result<(), anyhow::Error> {
            match req {
                RepositoryManagerRequest::Add { repo, responder } => {
                    assert_eq!(repo, test_repo_config());

                    responder.send(&mut Err(1)).unwrap()
                }
                RepositoryManagerRequest::Remove { repo_url: _, responder } => {
                    responder.send(&mut Ok(())).unwrap()
                }
                _ => {
                    panic!("unexpected RepositoryManager request {:?}", req);
                }
            }
            Ok(())
        }
    }

    struct FakeEngine {
        events: Arc<Mutex<Vec<EngineEvent>>>,
    }

    impl FakeEngine {
        fn new(
        ) -> (Self, impl Fn(&Context, Request<EngineMarker>) -> Result<(), anyhow::Error> + 'static)
        {
            Self::with_rules(vec![])
        }

        fn with_rules(
            rules: Vec<Rule>,
        ) -> (Self, impl Fn(&Context, Request<EngineMarker>) -> Result<(), anyhow::Error> + 'static)
        {
            let rules = Arc::new(Mutex::new(rules));
            let events = Arc::new(Mutex::new(Vec::new()));
            let events_closure = Arc::clone(&events);

            let closure = move |_cx: &Context, req| {
                match req {
                    EngineRequest::StartEditTransaction { transaction, control_handle: _ } => {
                        let rules = Arc::clone(&rules);
                        let events_closure = Arc::clone(&events_closure);
                        fasync::Task::local(async move {
                            let mut stream = transaction.into_stream().unwrap();
                            while let Some(request) = stream.next().await {
                                let request = request.unwrap();
                                match request {
                                    EditTransactionRequest::ResetAll { control_handle: _ } => {
                                        events_closure.lock().unwrap().push(EngineEvent::ResetAll);
                                    }
                                    EditTransactionRequest::ListDynamic {
                                        iterator,
                                        control_handle: _,
                                    } => {
                                        events_closure
                                            .lock()
                                            .unwrap()
                                            .push(EngineEvent::ListDynamic);
                                        let mut stream = iterator.into_stream().unwrap();

                                        let mut rules = rules.lock().unwrap().clone().into_iter();

                                        while let Some(req) = stream.try_next().await.unwrap() {
                                            let RuleIteratorRequest::Next { responder } = req;
                                            events_closure
                                                .lock()
                                                .unwrap()
                                                .push(EngineEvent::IteratorNext);

                                            if let Some(rule) = rules.next() {
                                                let rule = rule.into();
                                                responder.send(&mut vec![rule].iter_mut()).unwrap();
                                            } else {
                                                responder.send(&mut vec![].into_iter()).unwrap();
                                            }
                                        }
                                    }
                                    EditTransactionRequest::Add { rule, responder } => {
                                        events_closure.lock().unwrap().push(
                                            EngineEvent::EditTransactionAdd {
                                                rule: rule.try_into().unwrap(),
                                            },
                                        );
                                        responder.send(&mut Ok(())).unwrap()
                                    }
                                    EditTransactionRequest::Commit { responder } => {
                                        events_closure
                                            .lock()
                                            .unwrap()
                                            .push(EngineEvent::EditTransactionCommit);
                                        responder.send(&mut Ok(())).unwrap()
                                    }
                                }
                            }
                        })
                        .detach();
                    }
                    _ => panic!("unexpected request: {:?}", req),
                }

                Ok(())
            };

            (Self { events }, closure)
        }

        fn take_events(&self) -> Vec<EngineEvent> {
            self.events.lock().unwrap().drain(..).collect::<Vec<_>>()
        }
    }

    #[derive(Debug, PartialEq)]
    enum EngineEvent {
        ResetAll,
        ListDynamic,
        IteratorNext,
        EditTransactionAdd { rule: Rule },
        EditTransactionCommit,
    }

    #[derive(Default)]
    struct TestEventHandlerProvider;

    #[async_trait::async_trait(?Send)]
    impl EventHandlerProvider for TestEventHandlerProvider {
        async fn setup_event_handlers(&self, cx: Context, manager: Arc<RepositoryManager>) {
            let handler = TargetEventHandler::new(cx, manager, TARGET_NODENAME.to_string());
            handler.on_event(TargetEvent::RcsActivated).await.unwrap();
        }
    }

    async fn add_repo(proxy: &bridge::RepositoryRegistryProxy, repo_name: &str) {
        let path = fs::canonicalize(EMPTY_REPO_PATH).unwrap();

        let spec = RepositorySpec::FileSystem { path };
        proxy
            .add_repository(repo_name, &mut spec.into())
            .await
            .expect("communicated with proxy")
            .expect("adding repository to succeed");
    }

    async fn get_repositories(
        proxy: &bridge::RepositoryRegistryProxy,
    ) -> Vec<bridge::RepositoryConfig> {
        let (client, server) = fidl::endpoints::create_endpoints().unwrap();
        proxy.list_repositories(server).unwrap();
        let client = client.into_proxy().unwrap();

        let mut repositories = vec![];
        loop {
            let chunk = client.next().await.unwrap();
            if chunk.is_empty() {
                break;
            }
            repositories.extend(chunk);
        }

        repositories
    }

    async fn get_target_registrations(
        proxy: &bridge::RepositoryRegistryProxy,
    ) -> Vec<bridge::RepositoryTarget> {
        let (client, server) = fidl::endpoints::create_endpoints().unwrap();
        proxy.list_registered_targets(server).unwrap();
        let client = client.into_proxy().unwrap();

        let mut registrations = vec![];
        loop {
            let chunk = client.next().await.unwrap();
            if chunk.is_empty() {
                break;
            }
            registrations.extend(chunk);
        }

        registrations
    }

    // FIXME(http://fxbug.dev/80740): Unfortunately ffx_config is global, and so each of these tests
    // could step on each others ffx_config entries if run in parallel. To avoid this, we will:
    //
    // * use the `serial_test` crate to make sure each test runs sequentially
    // * clear out the config keys before we run each test to make sure state isn't leaked across
    //   tests.
    fn run_test<F: Future>(fut: F) -> F::Output {
        fuchsia_async::TestExecutor::new().unwrap().run_singlethreaded(async move {
            ffx_config::init(&[], None, None).unwrap();

            // Since ffx_config is global, it's possible to leave behind entries
            // across tests. Lets clean them up.
            let _ = pkg::config::remove_repository(REPO_NAME).await;
            let _ = pkg::config::remove_registration(REPO_NAME, TARGET_NODENAME).await;

            fut.await
        })
    }

    #[serial_test::serial]
    #[test]
    fn test_load_from_config_empty() {
        run_test(async {
            // Initialize a simple repository.
            ffx_config::set(("repository", ConfigLevel::User), serde_json::json!({}))
                .await
                .unwrap();

            let daemon = FakeDaemonBuilder::new()
                .register_fidl_service::<Repo<TestEventHandlerProvider>>()
                .build();
            let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;

            assert_eq!(get_repositories(&proxy).await, vec![]);
            assert_eq!(get_target_registrations(&proxy).await, vec![]);
        })
    }

    #[serial_test::serial]
    #[test]
    fn test_load_from_config_with_data() {
        run_test(async {
            // Initialize a simple repository.
            let repo_path =
                fs::canonicalize(EMPTY_REPO_PATH).unwrap().to_str().unwrap().to_string();

            ffx_config::set(
                ("repository", ConfigLevel::User),
                serde_json::json!({
                    "repositories": {
                        REPO_NAME: {
                            "type": "file_system",
                            "path": repo_path
                        },
                    },
                    "registrations": {
                        REPO_NAME: {
                            TARGET_NODENAME: {
                                "repo_name": REPO_NAME,
                                "target_identifier": TARGET_NODENAME,
                                "aliases": [ "fuchsia.com", "example.com" ],
                                "storage_type": "ephemeral",
                            },
                        }
                    }
                }),
            )
            .await
            .unwrap();

            let (fake_repo_manager, fake_repo_manager_closure) = FakeRepositoryManager::new();
            let (fake_engine, fake_engine_closure) = FakeEngine::new();

            let daemon = FakeDaemonBuilder::new()
                .register_instanced_service_closure::<RepositoryManagerMarker, _>(
                    fake_repo_manager_closure,
                )
                .register_instanced_service_closure::<EngineMarker, _>(fake_engine_closure)
                .register_fidl_service::<Repo<TestEventHandlerProvider>>()
                .nodename(TARGET_NODENAME.to_string())
                .build();

            let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;

            // Make sure we set up the repository and rewrite rules on the device.
            assert_eq!(
                fake_repo_manager.take_events(),
                vec![RepositoryManagerEvent::Add { repo: test_repo_config() }],
            );

            assert_eq!(
                fake_engine.take_events(),
                vec![
                    EngineEvent::ListDynamic,
                    EngineEvent::IteratorNext,
                    EngineEvent::ResetAll,
                    EngineEvent::EditTransactionAdd {
                        rule: rule!("fuchsia.com" => REPO_NAME, "/" => "/"),
                    },
                    EngineEvent::EditTransactionAdd {
                        rule: rule!("example.com" => REPO_NAME, "/" => "/"),
                    },
                    EngineEvent::EditTransactionCommit,
                ],
            );

            // Make sure we can read back the repositories.
            assert_eq!(
                get_repositories(&proxy).await,
                vec![bridge::RepositoryConfig {
                    name: REPO_NAME.to_string(),
                    spec: bridge::RepositorySpec::FileSystem(bridge::FileSystemRepositorySpec {
                        path: Some(repo_path.clone()),
                        ..bridge::FileSystemRepositorySpec::EMPTY
                    }),
                }]
            );

            // Make sure we can read back the taret registrations.
            assert_eq!(
                get_target_registrations(&proxy).await,
                vec![bridge::RepositoryTarget {
                    repo_name: Some(REPO_NAME.to_string()),
                    target_identifier: Some(TARGET_NODENAME.to_string()),
                    aliases: Some(vec!["fuchsia.com".to_string(), "example.com".to_string()]),
                    storage_type: Some(bridge::RepositoryStorageType::Ephemeral),
                    ..bridge::RepositoryTarget::EMPTY
                }],
            );
        });
    }

    #[serial_test::serial]
    #[test]
    fn test_add_remove() {
        run_test(async {
            let daemon = FakeDaemonBuilder::new()
                .register_fidl_service::<Repo<TestEventHandlerProvider>>()
                .build();

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

            // Make sure the repository was added.
            assert_eq!(
                get_repositories(&proxy).await,
                vec![bridge::RepositoryConfig { name: REPO_NAME.to_string(), spec }]
            );

            assert!(proxy.remove_repository(REPO_NAME).await.unwrap());

            // Make sure the repository was removed.
            assert_eq!(get_repositories(&proxy).await, vec![]);
        })
    }

    #[serial_test::serial]
    #[test]
    fn test_removing_repo_also_deregisters_from_target() {
        run_test(async {
            let (fake_repo_manager, fake_repo_manager_closure) = FakeRepositoryManager::new();
            let (fake_engine, fake_engine_closure) = FakeEngine::new();

            let daemon = FakeDaemonBuilder::new()
                .register_instanced_service_closure::<RepositoryManagerMarker, _>(
                    fake_repo_manager_closure,
                )
                .register_instanced_service_closure::<EngineMarker, _>(fake_engine_closure)
                .register_fidl_service::<Repo<TestEventHandlerProvider>>()
                .nodename(TARGET_NODENAME.to_string())
                .build();

            let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;

            // Make sure there is nothing in the registry.
            assert_eq!(fake_engine.take_events(), vec![]);
            assert_eq!(get_repositories(&proxy).await, vec![]);
            assert_eq!(get_target_registrations(&proxy).await, vec![]);

            add_repo(&proxy, REPO_NAME).await;

            // We shouldn't have added repositories or rewrite rules to the fuchsia device yet.

            assert_eq!(fake_repo_manager.take_events(), vec![]);
            assert_eq!(fake_engine.take_events(), vec![]);

            proxy
                .register_target(bridge::RepositoryTarget {
                    repo_name: Some(REPO_NAME.to_string()),
                    target_identifier: Some(TARGET_NODENAME.to_string()),
                    storage_type: Some(bridge::RepositoryStorageType::Ephemeral),
                    aliases: Some(vec!["fuchsia.com".to_string(), "example.com".to_string()]),
                    ..bridge::RepositoryTarget::EMPTY
                })
                .await
                .expect("communicated with proxy")
                .expect("target registration to succeed");

            // Registering the target should have set up a repository.
            assert_eq!(
                fake_repo_manager.take_events(),
                vec![RepositoryManagerEvent::Add { repo: test_repo_config() }]
            );

            // Adding the registration should have set up rewrite rules.
            assert_eq!(
                fake_engine.take_events(),
                vec![
                    EngineEvent::ListDynamic,
                    EngineEvent::IteratorNext,
                    EngineEvent::ResetAll,
                    EngineEvent::EditTransactionAdd {
                        rule: rule!("fuchsia.com" => REPO_NAME, "/" => "/"),
                    },
                    EngineEvent::EditTransactionAdd {
                        rule: rule!("example.com" => REPO_NAME, "/" => "/"),
                    },
                    EngineEvent::EditTransactionCommit,
                ],
            );

            // The RepositoryRegistry should remember we set up the registrations.
            assert_eq!(
                get_target_registrations(&proxy).await,
                vec![bridge::RepositoryTarget {
                    repo_name: Some(REPO_NAME.to_string()),
                    target_identifier: Some(TARGET_NODENAME.to_string()),
                    storage_type: Some(bridge::RepositoryStorageType::Ephemeral),
                    aliases: Some(vec!["fuchsia.com".to_string(), "example.com".to_string()]),
                    ..bridge::RepositoryTarget::EMPTY
                }],
            );

            // We should have saved the registration to the config.
            assert_matches!(
                pkg::config::get_registration(REPO_NAME, TARGET_NODENAME).await,
                Ok(Some(reg)) if reg == RepositoryTarget {
                    repo_name: "some-repo".to_string(),
                    target_identifier: Some("some-target".to_string()),
                    aliases: vec!["fuchsia.com".to_string(), "example.com".to_string()],
                    storage_type: Some(RepositoryStorageType::Ephemeral),
                }
            );

            assert!(proxy.remove_repository(REPO_NAME).await.expect("communicated with proxy"));

            // We should have removed the alias from the repository manager.
            assert_eq!(
                fake_engine.take_events(),
                vec![
                    EngineEvent::ListDynamic,
                    EngineEvent::IteratorNext,
                    EngineEvent::ResetAll,
                    EngineEvent::EditTransactionCommit,
                ]
            );

            assert_eq!(get_target_registrations(&proxy).await, vec![]);

            // The registration should have been cleared from the config.
            assert_matches!(
                pkg::config::get_registration(REPO_NAME, TARGET_NODENAME).await,
                Ok(None)
            );
        })
    }

    #[serial_test::serial]
    #[test]
    fn test_add_register_deregister() {
        run_test(async {
            let (fake_repo_manager, fake_repo_manager_closure) = FakeRepositoryManager::new();
            let (fake_engine, fake_engine_closure) = FakeEngine::new();

            let daemon = FakeDaemonBuilder::new()
                .register_instanced_service_closure::<RepositoryManagerMarker, _>(
                    fake_repo_manager_closure,
                )
                .register_instanced_service_closure::<EngineMarker, _>(fake_engine_closure)
                .register_fidl_service::<Repo<TestEventHandlerProvider>>()
                .nodename(TARGET_NODENAME.to_string())
                .build();

            let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;

            // Make sure there is nothing in the registry.
            assert_eq!(fake_engine.take_events(), vec![]);
            assert_eq!(get_repositories(&proxy).await, vec![]);
            assert_eq!(get_target_registrations(&proxy).await, vec![]);

            add_repo(&proxy, REPO_NAME).await;

            // We shouldn't have added repositories or rewrite rules to the fuchsia device yet.

            assert_eq!(fake_repo_manager.take_events(), vec![]);
            assert_eq!(fake_engine.take_events(), vec![]);

            proxy
                .register_target(bridge::RepositoryTarget {
                    repo_name: Some(REPO_NAME.to_string()),
                    target_identifier: Some(TARGET_NODENAME.to_string()),
                    storage_type: Some(bridge::RepositoryStorageType::Ephemeral),
                    aliases: Some(vec!["fuchsia.com".to_string(), "example.com".to_string()]),
                    ..bridge::RepositoryTarget::EMPTY
                })
                .await
                .expect("communicated with proxy")
                .expect("target registration to succeed");

            // Registering the target should have set up a repository.
            assert_eq!(
                fake_repo_manager.take_events(),
                vec![RepositoryManagerEvent::Add { repo: test_repo_config() }]
            );

            // Adding the registration should have set up rewrite rules.
            assert_eq!(
                fake_engine.take_events(),
                vec![
                    EngineEvent::ListDynamic,
                    EngineEvent::IteratorNext,
                    EngineEvent::ResetAll,
                    EngineEvent::EditTransactionAdd {
                        rule: rule!("fuchsia.com" => REPO_NAME, "/" => "/"),
                    },
                    EngineEvent::EditTransactionAdd {
                        rule: rule!("example.com" => REPO_NAME, "/" => "/"),
                    },
                    EngineEvent::EditTransactionCommit,
                ],
            );

            // The RepositoryRegistry should remember we set up the registrations.
            assert_eq!(
                get_target_registrations(&proxy).await,
                vec![bridge::RepositoryTarget {
                    repo_name: Some(REPO_NAME.to_string()),
                    target_identifier: Some(TARGET_NODENAME.to_string()),
                    storage_type: Some(bridge::RepositoryStorageType::Ephemeral),
                    aliases: Some(vec!["fuchsia.com".to_string(), "example.com".to_string(),]),
                    ..bridge::RepositoryTarget::EMPTY
                }],
            );

            // We should have saved the registration to the config.
            assert_matches!(
                pkg::config::get_registration(REPO_NAME, TARGET_NODENAME).await,
                Ok(Some(reg)) if reg == RepositoryTarget {
                    repo_name: "some-repo".to_string(),
                    target_identifier: Some("some-target".to_string()),
                    aliases: vec!["fuchsia.com".to_string(), "example.com".to_string()],
                    storage_type: Some(RepositoryStorageType::Ephemeral),
                }
            );

            proxy
                .deregister_target(REPO_NAME, Some(TARGET_NODENAME))
                .await
                .expect("communicated with proxy")
                .expect("target unregistration to succeed");

            // We should have removed the alias from the repository manager.
            assert_eq!(
                fake_engine.take_events(),
                vec![
                    EngineEvent::ListDynamic,
                    EngineEvent::IteratorNext,
                    EngineEvent::ResetAll,
                    EngineEvent::EditTransactionCommit,
                ]
            );

            assert_eq!(
                get_target_registrations(&proxy).await,
                vec![
                    /*
                    */
                ]
            );

            // The registration should have been cleared from the config.
            assert_matches!(
                pkg::config::get_registration(REPO_NAME, TARGET_NODENAME).await,
                Ok(None)
            );
        })
    }

    #[serial_test::serial]
    #[test]
    fn test_register_deduplicates_rules() {
        run_test(async {
            let (_fake_repo_manager, fake_repo_manager_closure) = FakeRepositoryManager::new();
            let (fake_engine, fake_engine_closure) = FakeEngine::with_rules(vec![
                rule!("fuchsia.com" => REPO_NAME, "/" => "/"),
                rule!("fuchsia.com" => "example.com", "/" => "/"),
                rule!("fuchsia.com" => "example.com", "/" => "/"),
                rule!("fuchsia.com" => "mycorp.com", "/" => "/"),
            ]);

            let daemon = FakeDaemonBuilder::new()
                .register_instanced_service_closure::<RepositoryManagerMarker, _>(
                    fake_repo_manager_closure,
                )
                .register_instanced_service_closure::<EngineMarker, _>(fake_engine_closure)
                .register_fidl_service::<Repo<TestEventHandlerProvider>>()
                .nodename(TARGET_NODENAME.to_string())
                .build();

            let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;
            add_repo(&proxy, REPO_NAME).await;

            proxy
                .register_target(bridge::RepositoryTarget {
                    repo_name: Some(REPO_NAME.to_string()),
                    target_identifier: Some(TARGET_NODENAME.to_string()),
                    storage_type: Some(bridge::RepositoryStorageType::Ephemeral),
                    aliases: Some(vec!["fuchsia.com".to_string(), "example.com".to_string()]),
                    ..bridge::RepositoryTarget::EMPTY
                })
                .await
                .expect("communicated with proxy")
                .expect("target registration to succeed");

            // Adding the registration should have set up rewrite rules.
            assert_eq!(
                fake_engine.take_events(),
                vec![
                    EngineEvent::ListDynamic,
                    EngineEvent::IteratorNext,
                    EngineEvent::IteratorNext,
                    EngineEvent::IteratorNext,
                    EngineEvent::IteratorNext,
                    EngineEvent::IteratorNext,
                    EngineEvent::ResetAll,
                    EngineEvent::EditTransactionAdd {
                        rule: rule!("fuchsia.com" => "mycorp.com", "/" => "/"),
                    },
                    EngineEvent::EditTransactionAdd {
                        rule: rule!("fuchsia.com" => "example.com", "/" => "/"),
                    },
                    EngineEvent::EditTransactionAdd {
                        rule: rule!("fuchsia.com" => REPO_NAME, "/" => "/"),
                    },
                    EngineEvent::EditTransactionAdd {
                        rule: rule!("example.com" => REPO_NAME, "/" => "/"),
                    },
                    EngineEvent::EditTransactionCommit,
                ],
            );
        })
    }

    #[serial_test::serial]
    #[test]
    fn test_remove_default_repository() {
        run_test(async {
            let (_fake_repo_manager, fake_repo_manager_closure) = FakeRepositoryManager::new();

            let daemon = FakeDaemonBuilder::new()
                .register_instanced_service_closure::<RepositoryManagerMarker, _>(
                    fake_repo_manager_closure,
                )
                .register_fidl_service::<Repo<TestEventHandlerProvider>>()
                .nodename(TARGET_NODENAME.to_string())
                .build();

            let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;
            add_repo(&proxy, REPO_NAME).await;

            let default_repo_name = "default-repo";
            pkg::config::set_default_repository(default_repo_name).await.unwrap();

            add_repo(&proxy, default_repo_name).await;

            // Remove the non-default repo, which shouldn't change the default repo.
            assert!(proxy.remove_repository(REPO_NAME).await.unwrap());
            assert_eq!(
                pkg::config::get_default_repository().await.unwrap(),
                Some(default_repo_name.to_string())
            );

            // Removing the default repository should also remove the config setting.
            assert!(proxy.remove_repository(default_repo_name).await.unwrap());
            assert_eq!(pkg::config::get_default_repository().await.unwrap(), None);
        })
    }

    #[serial_test::serial]
    #[test]
    fn test_add_register_default_target() {
        run_test(async {
            let (fake_repo_manager, fake_repo_manager_closure) = FakeRepositoryManager::new();
            let (fake_engine, fake_engine_closure) = FakeEngine::new();

            let daemon = FakeDaemonBuilder::new()
                .register_instanced_service_closure::<RepositoryManagerMarker, _>(
                    fake_repo_manager_closure,
                )
                .register_instanced_service_closure::<EngineMarker, _>(fake_engine_closure)
                .register_fidl_service::<Repo<TestEventHandlerProvider>>()
                .nodename(TARGET_NODENAME.to_string())
                .build();

            let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;
            add_repo(&proxy, REPO_NAME).await;

            proxy
                .register_target(bridge::RepositoryTarget {
                    repo_name: Some(REPO_NAME.to_string()),
                    target_identifier: None,
                    storage_type: Some(bridge::RepositoryStorageType::Ephemeral),
                    ..bridge::RepositoryTarget::EMPTY
                })
                .await
                .expect("communicated with proxy")
                .expect("target registration to succeed");

            assert_eq!(
                fake_repo_manager.take_events(),
                vec![RepositoryManagerEvent::Add { repo: test_repo_config() }]
            );

            // We didn't set up any aliases.
            assert_eq!(fake_engine.take_events(), vec![]);
        });
    }

    #[serial_test::serial]
    #[test]
    fn test_add_register_empty_aliases() {
        run_test(async {
            let (fake_repo_manager, fake_repo_manager_closure) = FakeRepositoryManager::new();
            let (fake_engine, fake_engine_closure) = FakeEngine::new();

            let daemon = FakeDaemonBuilder::new()
                .register_instanced_service_closure::<RepositoryManagerMarker, _>(
                    fake_repo_manager_closure,
                )
                .register_instanced_service_closure::<EngineMarker, _>(fake_engine_closure)
                .register_fidl_service::<Repo<TestEventHandlerProvider>>()
                .nodename(TARGET_NODENAME.to_string())
                .build();

            let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;
            add_repo(&proxy, REPO_NAME).await;

            // Make sure there's no repositories or registrations on the device.
            assert_eq!(fake_repo_manager.take_events(), vec![]);
            assert_eq!(fake_engine.take_events(), vec![]);

            // Make sure the registry doesn't have any registrations.
            assert_eq!(get_target_registrations(&proxy).await, vec![]);

            proxy
                .register_target(bridge::RepositoryTarget {
                    repo_name: Some(REPO_NAME.to_string()),
                    target_identifier: Some(TARGET_NODENAME.to_string()),
                    storage_type: Some(bridge::RepositoryStorageType::Ephemeral),
                    aliases: Some(vec![]),
                    ..bridge::RepositoryTarget::EMPTY
                })
                .await
                .unwrap()
                .unwrap();

            // We should have added a repository to the device, but no rewrite rules.
            assert_eq!(
                fake_repo_manager.take_events(),
                vec![RepositoryManagerEvent::Add { repo: test_repo_config() }]
            );
            assert_eq!(fake_engine.take_events(), vec![]);

            // Make sure we can query the registration.
            assert_eq!(
                get_target_registrations(&proxy).await,
                vec![bridge::RepositoryTarget {
                    repo_name: Some(REPO_NAME.to_string()),
                    target_identifier: Some(TARGET_NODENAME.to_string()),
                    storage_type: Some(bridge::RepositoryStorageType::Ephemeral),
                    aliases: Some(vec![]),
                    ..bridge::RepositoryTarget::EMPTY
                }],
            );
        });
    }

    #[serial_test::serial]
    #[test]
    fn test_add_register_none_aliases() {
        run_test(async {
            let (fake_repo_manager, fake_repo_manager_closure) = FakeRepositoryManager::new();
            let (fake_engine, fake_engine_closure) = FakeEngine::new();

            let daemon = FakeDaemonBuilder::new()
                .register_instanced_service_closure::<RepositoryManagerMarker, _>(
                    fake_repo_manager_closure,
                )
                .register_instanced_service_closure::<EngineMarker, _>(fake_engine_closure)
                .register_fidl_service::<Repo<TestEventHandlerProvider>>()
                .nodename(TARGET_NODENAME.to_string())
                .build();

            let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;
            add_repo(&proxy, REPO_NAME).await;

            proxy
                .register_target(bridge::RepositoryTarget {
                    repo_name: Some(REPO_NAME.to_string()),
                    target_identifier: Some(TARGET_NODENAME.to_string()),
                    storage_type: Some(bridge::RepositoryStorageType::Ephemeral),
                    aliases: None,
                    ..bridge::RepositoryTarget::EMPTY
                })
                .await
                .unwrap()
                .unwrap();

            // Make sure we set up the repository on the device.
            assert_eq!(
                fake_repo_manager.take_events(),
                vec![RepositoryManagerEvent::Add { repo: test_repo_config() }],
            );

            // We shouldn't have made any rewrite rules.
            assert_eq!(fake_engine.take_events(), vec![]);

            assert_eq!(
                get_target_registrations(&proxy).await,
                vec![bridge::RepositoryTarget {
                    repo_name: Some(REPO_NAME.to_string()),
                    target_identifier: Some(TARGET_NODENAME.to_string()),
                    storage_type: Some(bridge::RepositoryStorageType::Ephemeral),
                    aliases: Some(vec![]),
                    ..bridge::RepositoryTarget::EMPTY
                }],
            );
        })
    }

    #[serial_test::serial]
    #[test]
    fn test_add_register_repo_manager_error() {
        run_test(async {
            let (fake_engine, fake_engine_closure) = FakeEngine::new();

            let daemon = FakeDaemonBuilder::new()
                .register_fidl_service::<ErroringRepositoryManager>()
                .register_instanced_service_closure::<EngineMarker, _>(fake_engine_closure)
                .register_fidl_service::<Repo<TestEventHandlerProvider>>()
                .nodename(TARGET_NODENAME.to_string())
                .build();

            let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;
            add_repo(&proxy, REPO_NAME).await;

            assert_eq!(
                proxy
                    .register_target(bridge::RepositoryTarget {
                        repo_name: Some(REPO_NAME.to_string()),
                        target_identifier: Some(TARGET_NODENAME.to_string()),
                        storage_type: Some(bridge::RepositoryStorageType::Ephemeral),
                        aliases: None,
                        ..bridge::RepositoryTarget::EMPTY
                    })
                    .await
                    .unwrap()
                    .unwrap_err(),
                bridge::RepositoryError::RepositoryManagerError
            );

            // Make sure we didn't communicate with the device.
            assert_eq!(fake_engine.take_events(), vec![]);

            // Make sure the repository registration wasn't added.
            assert_eq!(get_target_registrations(&proxy).await, vec![]);

            // Make sure nothing was saved to the config.
            assert_matches!(
                pkg::config::get_registration(REPO_NAME, TARGET_NODENAME).await,
                Ok(None)
            );
        });
    }

    #[serial_test::serial]
    #[test]
    fn test_register_non_existent_repo() {
        run_test(async {
            let (fake_engine, fake_engine_closure) = FakeEngine::new();

            let daemon = FakeDaemonBuilder::new()
                .register_fidl_service::<ErroringRepositoryManager>()
                .register_instanced_service_closure::<EngineMarker, _>(fake_engine_closure)
                .register_fidl_service::<Repo<TestEventHandlerProvider>>()
                .nodename(TARGET_NODENAME.to_string())
                .build();

            let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;
            assert_eq!(
                proxy
                    .register_target(bridge::RepositoryTarget {
                        repo_name: Some(REPO_NAME.to_string()),
                        target_identifier: Some(TARGET_NODENAME.to_string()),
                        storage_type: Some(bridge::RepositoryStorageType::Ephemeral),
                        aliases: None,
                        ..bridge::RepositoryTarget::EMPTY
                    })
                    .await
                    .unwrap()
                    .unwrap_err(),
                bridge::RepositoryError::NoMatchingRepository
            );

            // Make sure we didn't communicate with the device.
            assert_eq!(fake_engine.take_events(), vec![]);
        })
    }

    #[serial_test::serial]
    #[test]
    fn test_deregister_non_existent_repo() {
        run_test(async {
            let (fake_engine, fake_engine_closure) = FakeEngine::new();

            let daemon = FakeDaemonBuilder::new()
                .register_fidl_service::<ErroringRepositoryManager>()
                .register_instanced_service_closure::<EngineMarker, _>(fake_engine_closure)
                .register_fidl_service::<Repo<TestEventHandlerProvider>>()
                .nodename(TARGET_NODENAME.to_string())
                .build();

            let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;
            assert_eq!(
                proxy
                    .deregister_target(REPO_NAME, Some(TARGET_NODENAME))
                    .await
                    .unwrap()
                    .unwrap_err(),
                bridge::RepositoryError::NoMatchingRepository
            );

            // Make sure we didn't communicate with the device.
            assert_eq!(fake_engine.take_events(), vec![]);
        });
    }
}
