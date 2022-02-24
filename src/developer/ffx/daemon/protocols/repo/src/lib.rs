// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::tunnel::TunnelManager,
    anyhow::{Context as _, Result},
    async_lock::RwLock,
    async_trait::async_trait,
    ffx_daemon_core::events::{EventHandler, Status as EventStatus},
    ffx_daemon_events::{DaemonEvent, TargetEvent, TargetInfo},
    ffx_daemon_target::target::Target,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_developer_bridge as bridge,
    fidl_fuchsia_developer_bridge_ext::{RepositorySpec, RepositoryTarget},
    fidl_fuchsia_pkg::RepositoryManagerMarker,
    fidl_fuchsia_pkg_rewrite::EngineMarker,
    fidl_fuchsia_pkg_rewrite_ext::{do_transaction, Rule},
    fuchsia_async as fasync,
    fuchsia_hyper::{new_https_client, HttpsClient},
    fuchsia_zircon_status::Status,
    futures::{FutureExt as _, StreamExt as _},
    itertools::Itertools as _,
    pkg::{
        config as pkg_config,
        repository::{
            self, FileSystemRepository, HttpRepository, PmRepository, Repository,
            RepositoryBackend, RepositoryManager, RepositoryServer,
        },
    },
    protocols::prelude::*,
    std::{
        collections::HashSet,
        convert::{TryFrom, TryInto},
        net::SocketAddr,
        rc::Rc,
        sync::Arc,
        time::Duration,
    },
    url::Url,
};

mod metrics;
mod tunnel;

const REPOSITORY_MANAGER_SELECTOR: &str = "core/appmgr:out:fuchsia.pkg.RepositoryManager";
const REWRITE_PROTOCOL_SELECTOR: &str = "core/appmgr:out:fuchsia.pkg.rewrite.Engine";

const TARGET_CONNECT_TIMEOUT: Duration = Duration::from_secs(5);
const SHUTDOWN_TIMEOUT: Duration = Duration::from_secs(5);

const MAX_PACKAGES: i64 = 512;
const MAX_REGISTERED_TARGETS: i64 = 512;

#[derive(Debug)]
struct ServerInfo {
    server: RepositoryServer,
    task: fasync::Task<()>,
    tunnel_manager: TunnelManager,
}

#[derive(Debug)]
enum ServerState {
    Running(ServerInfo),
    Stopped(SocketAddr),
    Unconfigured,
}

impl ServerState {
    async fn new_running(addr: SocketAddr, manager: Arc<RepositoryManager>) -> Result<Self> {
        log::info!("Starting repository server on {}", addr);

        let (server_fut, sink, server) = RepositoryServer::builder(addr, Arc::clone(&manager))
            .start()
            .await
            .context("starting repository server")?;

        log::info!("Started repository server on {}", server.local_addr());

        // Spawn the server future in the background to process requests from clients.
        let task = fasync::Task::local(server_fut);

        let tunnel_manager = TunnelManager::new(server.local_addr(), sink);

        Ok(ServerState::Running(ServerInfo { server, task, tunnel_manager }))
    }

    async fn start_tunnel(&self, cx: &Context, target_nodename: &str) -> Result<()> {
        match self {
            ServerState::Running(ref server_info) => {
                server_info.tunnel_manager.start_tunnel(cx, target_nodename.to_string()).await
            }
            _ => Ok(()),
        }
    }

    async fn stop(&mut self) {
        match std::mem::replace(self, ServerState::Unconfigured) {
            ServerState::Running(server_info) => {
                *self = ServerState::Stopped(server_info.server.local_addr());

                log::info!("Stopping the repository server");

                server_info.server.stop();

                futures::select! {
                    () = server_info.task.fuse() => {
                        log::info!("Stopped the repository server");
                    },
                    () = fasync::Timer::new(SHUTDOWN_TIMEOUT).fuse() => {
                        log::error!("Timed out waiting for the repository server to shut down");
                    },
                }
            }
            state => {
                *self = state;
            }
        }
    }

    /// Returns the address is running on. Returns None if the server is not
    /// running, or is unconfigured.
    fn listen_addr(&self) -> Option<SocketAddr> {
        match self {
            ServerState::Running(x) => Some(x.server.local_addr()),
            _ => None,
        }
    }
}

// TODO: Whatever has to be done to make this private again.
pub struct RepoInner {
    manager: Arc<RepositoryManager>,
    server: ServerState,
    https_client: HttpsClient,
}

impl RepoInner {
    fn new() -> Arc<RwLock<Self>> {
        Arc::new(RwLock::new(RepoInner {
            manager: RepositoryManager::new(),
            server: ServerState::Unconfigured,
            https_client: new_https_client(),
        }))
    }
}

#[ffx_protocol]
pub struct Repo<T: EventHandlerProvider = RealEventHandlerProvider> {
    inner: Arc<RwLock<RepoInner>>,
    event_handler_provider: T,
}

#[derive(PartialEq)]
enum SaveConfig {
    Save,
    DoNotSave,
}

#[async_trait::async_trait(?Send)]
pub trait EventHandlerProvider {
    async fn setup_event_handlers(&mut self, cx: Context, inner: Arc<RwLock<RepoInner>>);
}

#[derive(Default)]
pub struct RealEventHandlerProvider;

#[async_trait::async_trait(?Send)]
impl EventHandlerProvider for RealEventHandlerProvider {
    async fn setup_event_handlers(&mut self, cx: Context, inner: Arc<RwLock<RepoInner>>) {
        let q = cx.daemon_event_queue().await;
        q.add_handler(DaemonEventHandler { cx, inner }).await;
    }
}

async fn start_tunnel(
    cx: &Context,
    inner: &Arc<RwLock<RepoInner>>,
    target_nodename: &str,
) -> Result<()> {
    inner.read().await.server.start_tunnel(&cx, &target_nodename).await
}

async fn repo_spec_to_backend(
    repo_spec: &RepositorySpec,
    inner: &Arc<RwLock<RepoInner>>,
) -> Result<Box<dyn RepositoryBackend + Send + Sync>, bridge::RepositoryError> {
    match repo_spec {
        RepositorySpec::FileSystem { metadata_repo_path, blob_repo_path } => Ok(Box::new(
            FileSystemRepository::new(metadata_repo_path.into(), blob_repo_path.into()),
        )),
        RepositorySpec::Pm { path } => Ok(Box::new(PmRepository::new(path.into()))),
        RepositorySpec::Http { metadata_repo_url, blob_repo_url } => {
            let metadata_repo_url = Url::parse(metadata_repo_url.as_str()).map_err(|err| {
                log::error!("Unable to parse metadata repo url {}: {:#}", metadata_repo_url, err);
                bridge::RepositoryError::InvalidUrl
            })?;

            let blob_repo_url = Url::parse(blob_repo_url.as_str()).map_err(|err| {
                log::error!("Unable to parse blob repo url {}: {:#}", blob_repo_url, err);
                bridge::RepositoryError::InvalidUrl
            })?;

            let https_client = inner.read().await.https_client.clone();

            Ok(Box::new(HttpRepository::new(https_client, metadata_repo_url, blob_repo_url)))
        }
    }
}

async fn add_repository(
    repo_name: &str,
    repo_spec: &RepositorySpec,
    save_config: SaveConfig,
    inner: Arc<RwLock<RepoInner>>,
) -> Result<(), bridge::RepositoryError> {
    log::info!("Adding repository {} {:?}", repo_name, repo_spec);

    // Create the repository.
    let backend = repo_spec_to_backend(&repo_spec, &inner).await?;
    let repo = Repository::new(repo_name, backend).await.map_err(|err| {
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
    let mut inner = inner.write().await;
    inner.manager.add(Arc::new(repo));

    // The repository server is only started when repositories are added to the
    // daemon. Now that we added one, make sure the server has started.
    inner.start_server_warn().await;

    metrics::add_repository_event(&repo_spec).await;

    Ok(())
}

async fn register_target(
    cx: &Context,
    mut target_info: RepositoryTarget,
    save_config: SaveConfig,
    inner: Arc<RwLock<RepoInner>>,
) -> Result<(), bridge::RepositoryError> {
    log::info!(
        "Registering repository {:?} for target {:?}",
        target_info.repo_name,
        target_info.target_identifier
    );

    let repo = inner
        .read()
        .await
        .manager
        .get(&target_info.repo_name)
        .ok_or_else(|| bridge::RepositoryError::NoMatchingRepository)?;

    let (target, proxy) = futures::select! {
        res = cx.open_target_proxy_with_info::<RepositoryManagerMarker>(
            target_info.target_identifier.clone(),
            REPOSITORY_MANAGER_SELECTOR,
        ).fuse() => {
            res.map_err(|err| {
                log::error!(
                    "failed to open target proxy with target name {:?}: {:#?}",
                    target_info.target_identifier,
                    err
                );
                bridge::RepositoryError::TargetCommunicationFailure
            })?
        }
        _ = fasync::Timer::new(TARGET_CONNECT_TIMEOUT).fuse() => {
            log::error!("Timed out connecting to target name {:?}", target_info.target_identifier);
            return Err(bridge::RepositoryError::TargetCommunicationFailure);
        }
    };

    let target_nodename = target.nodename.ok_or_else(|| {
        log::error!("target {:?} does not have a nodename", target_info.target_identifier);
        bridge::RepositoryError::InternalError
    })?;

    let listen_addr = match inner.read().await.server.listen_addr() {
        Some(listen_addr) => listen_addr,
        None => {
            log::error!("repository server is not running");
            return Err(bridge::RepositoryError::ServerNotRunning);
        }
    };

    // Before we register the repository, we need to decide which address the
    // target device should use to reach the repository. If the server is
    // running on a loopback device, then we need to create a tunnel for the
    // device to access the server.
    let (should_make_tunnel, repo_host) = create_repo_host(
        listen_addr,
        target.ssh_host_address.ok_or_else(|| {
            log::error!("target {:?} does not have a host address", target_info.target_identifier);
            bridge::RepositoryError::InternalError
        })?,
    );

    let config = repo
        .get_config(
            &format!("{}/{}", repo_host, repo.name()),
            target_info.storage_type.clone().map(|storage_type| storage_type.into()),
        )
        .await
        .map_err(|e| {
            log::error!("failed to get config: {}", e);
            return bridge::RepositoryError::RepositoryManagerError;
        })?;

    match proxy.add(config.into()).await {
        Ok(Ok(())) => {}
        Ok(Err(err)) => {
            log::error!("failed to add config: {:#?}", Status::from_raw(err));
            return Err(bridge::RepositoryError::RepositoryManagerError);
        }
        Err(err) => {
            log::error!("failed to add config: {:#?}", err);
            return Err(bridge::RepositoryError::TargetCommunicationFailure);
        }
    }

    if !target_info.aliases.is_empty() {
        let () = create_aliases(cx, repo.name(), &target_nodename, &target_info.aliases).await?;
    }

    if should_make_tunnel {
        // Start the tunnel to the device if one isn't running already.
        start_tunnel(&cx, &inner, &target_nodename).await.map_err(|err| {
            log::error!("Failed to start tunnel to target {:?}: {:#}", target_nodename, err);
            bridge::RepositoryError::TargetCommunicationFailure
        })?;
    }

    if save_config == SaveConfig::Save {
        // Make sure we update the target info with the real nodename.
        target_info.target_identifier = Some(target_nodename.clone());

        pkg::config::set_registration(&target_nodename, &target_info).await.map_err(|err| {
            log::error!("Failed to save registration to config: {:#?}", err);
            bridge::RepositoryError::InternalError
        })?;
    }

    Ok(())
}

/// Decide which repo host we should use when creating a repository config, and
/// whether or not we need to create a tunnel in order for the device to talk to
/// the repository.
fn create_repo_host(
    listen_addr: SocketAddr,
    host_address: bridge::SshHostAddrInfo,
) -> (bool, String) {
    // We need to decide which address the target device should use to reach the
    // repository. If the server is running on a loopback device, then we need
    // to create a tunnel for the device to access the server.
    if listen_addr.ip().is_loopback() {
        return (true, listen_addr.to_string());
    }

    // However, if it's not a loopback address, then configure the device to
    // communicate by way of the ssh host's address. This is helpful when the
    // device can access the repository only through a specific interface.

    // FIXME(http://fxbug.dev/87439): Once the tunnel bug is fixed, we may
    // want to default all traffic going through the tunnel. Consider
    // creating an ffx config variable to decide if we want to always
    // tunnel, or only tunnel if the server is on a loopback address.

    // IPv6 addresses can contain a ':', IPv4 cannot.
    let repo_host = if host_address.address.contains(':') {
        if let Some(pos) = host_address.address.rfind('%') {
            let ip = &host_address.address[..pos];
            let scope_id = &host_address.address[pos + 1..];
            format!("[{}%25{}]:{}", ip, scope_id, listen_addr.port())
        } else {
            format!("[{}]:{}", host_address.address, listen_addr.port())
        }
    } else {
        format!("{}:{}", host_address.address, listen_addr.port())
    };

    (false, repo_host)
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
            REWRITE_PROTOCOL_SELECTOR,
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
            REWRITE_PROTOCOL_SELECTOR,
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

impl RepoInner {
    async fn start_server(&mut self) -> Result<(), anyhow::Error> {
        // Exit early if we're already running on this address.
        let addr = match self.server {
            ServerState::Running(_) | ServerState::Unconfigured => return Ok(()),
            ServerState::Stopped(addr) => addr.clone(),
        };

        match ServerState::new_running(addr, Arc::clone(&self.manager)).await {
            Ok(server) => {
                self.server = server;
                metrics::server_started_event().await;
            }
            Err(err) => {
                metrics::server_failed_to_start_event(&err.to_string()).await;
                return Err(err);
            }
        }

        Ok(())
    }

    async fn start_server_warn(&mut self) {
        if let Err(err) = self.start_server().await {
            log::error!("Failed to start repository server: {:#?}", err);
        }
    }

    async fn stop_server(&mut self) {
        log::info!("Stopping repository protocol");

        self.server.stop().await;

        // Drop all repositories.
        self.manager.clear();

        log::info!("Repository protocol has been stopped");
    }
}

impl<T: EventHandlerProvider> Repo<T> {
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
        let mut inner = self.inner.write().await;
        let ret = inner.manager.remove(repo_name);

        if inner.manager.repositories().next().is_none() {
            inner.stop_server().await;
        }

        ret
    }

    /// Deregister the repository from the target.
    ///
    /// This only works for repositories managed by `ffx`. If the repository named `repo_name` is
    /// unknown to this protocol, error out rather than trying to remove the registration.
    async fn deregister_target(
        &self,
        cx: &Context,
        repo_name: String,
        target_identifier: Option<String>,
    ) -> Result<(), bridge::RepositoryError> {
        log::info!("Deregistering repository {:?} from target {:?}", repo_name, target_identifier);

        let repo = self
            .inner
            .read()
            .await
            .manager
            .get(&repo_name)
            .ok_or_else(|| bridge::RepositoryError::NoMatchingRepository)?;

        // Connect to the target. Error out if we can't connect, since we can't remove the registration
        // from it.
        let (target, proxy) = futures::select! {
            res = cx.open_target_proxy_with_info::<RepositoryManagerMarker>(
                target_identifier.clone(),
                REPOSITORY_MANAGER_SELECTOR,
            ).fuse() => {
                res.map_err(|err| {
                    log::warn!(
                        "Failed to open target proxy with target name {:?}: {:#?}",
                        target_identifier,
                        err
                    );
                    bridge::RepositoryError::TargetCommunicationFailure
                })?
            },
            _ = fasync::Timer::new(TARGET_CONNECT_TIMEOUT).fuse() => {
                log::error!("Timed out connecting to target name {:?}", target_identifier);
                return Err(bridge::RepositoryError::TargetCommunicationFailure);
            }
        };

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

        let repo = if let Some(r) = self.inner.read().await.manager.get(&name) {
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

        let repo = if let Some(r) = self.inner.read().await.manager.get(&repository_name) {
            r
        } else {
            return Err(bridge::RepositoryError::NoMatchingRepository);
        };

        let values = repo.show_package(package_name.to_owned()).await.map_err(|err| {
            log::error!("Unable to list package contents {:?}: {}", package_name, err);
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
        Repo { inner: RepoInner::new(), event_handler_provider: T::default() }
    }
}

#[async_trait(?Send)]
impl<T: EventHandlerProvider + Default + Unpin + 'static> FidlProtocol for Repo<T> {
    type Protocol = bridge::RepositoryRegistryMarker;
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
                        add_repository(&name, &repo_spec, SaveConfig::Save, Arc::clone(&self.inner))
                            .await
                    }
                    Err(err) => Err(err.into()),
                };

                responder.send(&mut res)?;

                Ok(())
            }
            bridge::RepositoryRegistryRequest::RemoveRepository { name, responder } => {
                responder.send(self.remove_repository(cx, &name).await)?;

                metrics::remove_repository_event().await;

                Ok(())
            }
            bridge::RepositoryRegistryRequest::RegisterTarget { target_info, responder } => {
                let mut res = match RepositoryTarget::try_from(target_info) {
                    Ok(target_info) => {
                        register_target(cx, target_info, SaveConfig::Save, Arc::clone(&self.inner))
                            .await
                    }
                    Err(err) => Err(err.into()),
                };

                responder.send(&mut res)?;

                metrics::register_repository_event().await;

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

                metrics::deregister_repository_event().await;

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
                let mut values = {
                    let inner = self.inner.read().await;

                    inner
                        .manager
                        .repositories()
                        .map(|x| bridge::RepositoryConfig {
                            name: x.name().to_owned(),
                            spec: x.spec().into(),
                        })
                        .collect::<Vec<_>>()
                };
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
        log::info!("Starting repository protocol");

        match pkg_config::repository_server_mode().await {
            Ok(mode) => {
                metrics::server_mode_event(&mode).await;

                if mode == "ffx" {
                    match pkg_config::repository_listen_addr().await {
                        Ok(Some(addr)) => {
                            let mut inner = self.inner.write().await;
                            inner.server = ServerState::Stopped(addr);
                        }
                        Ok(None) => {
                            log::error!(
                                "repository.server.listen address not configured, not starting server"
                            );

                            metrics::server_disabled_event().await;
                        }
                        Err(err) => {
                            log::error!("Failed to read server address from config: {:#}", err);
                        }
                    }
                } else {
                    log::warn!("Repository server mode is {:?}, not starting server", mode);
                }
            }
            Err(err) => {
                log::error!("Failed to determine if server is enabled from config: {:#}", err);
            }
        }

        load_repositories_from_config(&self.inner).await;

        self.event_handler_provider.setup_event_handlers(cx.clone(), Arc::clone(&self.inner)).await;

        Ok(())
    }

    async fn stop(&mut self, _cx: &Context) -> Result<(), anyhow::Error> {
        self.inner.write().await.stop_server().await;
        Ok(())
    }
}

async fn load_repositories_from_config(inner: &Arc<RwLock<RepoInner>>) {
    for (name, repo_spec) in pkg::config::get_repositories().await {
        if inner.read().await.manager.get(&name).is_some() {
            continue;
        }

        // Add the repository.
        if let Err(err) =
            add_repository(&name, &repo_spec, SaveConfig::DoNotSave, Arc::clone(inner)).await
        {
            log::warn!("failed to add the repository {:?}: {:?}", name, err);
        }
    }
}

#[derive(Clone)]
struct DaemonEventHandler {
    cx: Context,
    inner: Arc<RwLock<RepoInner>>,
}

impl DaemonEventHandler {
    /// pub(crate) so that this is visible to tests.
    pub(crate) fn build_matcher(t: TargetInfo) -> Option<String> {
        if let Some(nodename) = t.nodename {
            Some(nodename)
        } else {
            // If this target doesn't have a nodename, we fall back to matching on IP/port.
            // Since this is only used for matching and not connecting,
            // we simply choose the first address in the list.
            if let Some(addr) = t.addresses.first() {
                let addr_str =
                    if addr.ip().is_ipv6() { format!("[{}]", addr) } else { format!("{}", addr) };

                if let Some(p) = t.ssh_port.as_ref() {
                    Some(format!("{}:{}", addr_str, p))
                } else {
                    Some(format!("{}", addr))
                }
            } else {
                None
            }
        }
    }
}

#[async_trait(?Send)]
impl EventHandler<DaemonEvent> for DaemonEventHandler {
    async fn on_event(&self, event: DaemonEvent) -> Result<EventStatus> {
        match event {
            DaemonEvent::NewTarget(info) => {
                let matcher = if let Some(s) = Self::build_matcher(info) {
                    s
                } else {
                    return Ok(EventStatus::Waiting);
                };
                let (t, q) = self.cx.get_target_event_queue(Some(matcher)).await?;
                q.add_handler(TargetEventHandler::new(self.cx.clone(), Arc::clone(&self.inner), t))
                    .await;
            }
            _ => {}
        }
        Ok(EventStatus::Waiting)
    }
}

#[derive(Clone)]
struct TargetEventHandler {
    cx: Context,
    inner: Arc<RwLock<RepoInner>>,
    target: Rc<Target>,
}

impl TargetEventHandler {
    fn new(cx: Context, inner: Arc<RwLock<RepoInner>>, target: Rc<Target>) -> Self {
        Self { cx, inner, target }
    }
}

#[async_trait(?Send)]
impl EventHandler<TargetEvent> for TargetEventHandler {
    async fn on_event(&self, event: TargetEvent) -> Result<EventStatus> {
        if !matches!(event, TargetEvent::RcsActivated) {
            return Ok(EventStatus::Waiting);
        }

        // Make sure we pick up any repositories that have been added since the last event.
        load_repositories_from_config(&self.inner).await;

        let source_nodename = if let Some(n) = self.target.nodename() {
            n
        } else {
            log::warn!("not registering target due to missing nodename {:?}", self.target);
            return Ok(EventStatus::Waiting);
        };

        // Find any saved registrations for this target and register them on the device.
        for (repo_name, targets) in pkg::config::get_registrations().await {
            log::info!("registrations {:?} {:?}", repo_name, targets);
            for (target_nodename, target_info) in targets {
                if target_nodename != source_nodename {
                    continue;
                }

                if let Err(err) = register_target(
                    &self.cx,
                    target_info,
                    SaveConfig::DoNotSave,
                    Arc::clone(&self.inner),
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
        addr::TargetAddr,
        assert_matches::assert_matches,
        ffx_config::ConfigLevel,
        fidl::{self, endpoints::Request},
        fidl_fuchsia_developer_bridge_ext::RepositoryStorageType,
        fidl_fuchsia_developer_remotecontrol as rcs,
        fidl_fuchsia_pkg::{
            MirrorConfig, RepositoryConfig, RepositoryKeyConfig, RepositoryManagerRequest,
        },
        fidl_fuchsia_pkg_rewrite::{
            EditTransactionRequest, EngineMarker, EngineRequest, RuleIteratorRequest,
        },
        futures::TryStreamExt,
        protocols::testing::FakeDaemonBuilder,
        std::{
            cell::RefCell,
            convert::TryInto,
            fs,
            future::Future,
            net::{Ipv4Addr, Ipv6Addr, SocketAddr},
            rc::Rc,
            sync::{Arc, Mutex},
        },
    };

    const REPO_NAME: &str = "some-repo";
    const TARGET_NODENAME: &str = "some-target";
    const HOST_ADDR: &str = "1.2.3.4";
    const EMPTY_REPO_PATH: &str = "host_x64/test_data/ffx_daemon_protocol_repo/empty-repo";

    macro_rules! rule {
        ($host_match:expr => $host_replacement:expr,
         $path_prefix_match:expr => $path_prefix_replacement:expr) => {
            Rule::new($host_match, $host_replacement, $path_prefix_match, $path_prefix_replacement)
                .unwrap()
        };
    }

    async fn test_repo_config(
        repo: &Rc<RefCell<Repo<TestEventHandlerProvider>>>,
    ) -> RepositoryConfig {
        test_repo_config_with_repo_host(repo, None).await
    }

    async fn test_repo_config_with_repo_host(
        repo: &Rc<RefCell<Repo<TestEventHandlerProvider>>>,
        repo_host: Option<String>,
    ) -> RepositoryConfig {
        // The repository server started on a random address, so look it up.
        let inner = Arc::clone(&repo.borrow().inner);
        let addr = if let Some(addr) = inner.read().await.server.listen_addr() {
            addr
        } else {
            panic!("server is not running");
        };

        let repo_host = if let Some(repo_host) = repo_host {
            format!("{}:{}", repo_host, addr.port())
        } else {
            addr.to_string()
        };

        RepositoryConfig {
            mirrors: Some(vec![MirrorConfig {
                mirror_url: Some(format!("http://{}/{}", repo_host, REPO_NAME)),
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

    struct ErroringRepositoryManager {
        events: Arc<Mutex<Vec<RepositoryManagerEvent>>>,
    }

    impl ErroringRepositoryManager {
        fn new() -> (
            Self,
            impl Fn(&Context, Request<RepositoryManagerMarker>) -> Result<(), anyhow::Error> + 'static,
        ) {
            let events = Arc::new(Mutex::new(Vec::new()));
            let events_closure = Arc::clone(&events);

            let closure = move |_cx: &Context, req| match req {
                RepositoryManagerRequest::Add { repo, responder } => {
                    events_closure.lock().unwrap().push(RepositoryManagerEvent::Add { repo });
                    responder.send(&mut Err(1)).unwrap();
                    Ok(())
                }
                RepositoryManagerRequest::Remove { repo_url: _, responder } => {
                    responder.send(&mut Ok(())).unwrap();
                    Ok(())
                }
                _ => {
                    panic!("unexpected RepositoryManager request {:?}", req);
                }
            };

            (Self { events }, closure)
        }

        fn take_events(&self) -> Vec<RepositoryManagerEvent> {
            self.events.lock().unwrap().drain(..).collect::<Vec<_>>()
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

    struct FakeRcs {
        events: Arc<Mutex<Vec<RcsEvent>>>,
    }

    impl FakeRcs {
        fn new() -> (Self, impl Fn(rcs::RemoteControlRequest, Option<String>) -> ()) {
            let events = Arc::new(Mutex::new(Vec::new()));
            let events_closure = Arc::clone(&events);

            let closure = move |req: rcs::RemoteControlRequest, target: Option<String>| {
                log::info!("got a rcs request: {:?} {:?}", req, target);

                match (req, target.as_deref()) {
                    (
                        rcs::RemoteControlRequest::ReverseTcp { responder, .. },
                        Some(TARGET_NODENAME),
                    ) => {
                        events_closure.lock().unwrap().push(RcsEvent::ReverseTcp);
                        responder.send(&mut Ok(())).unwrap()
                    }
                    (req, target) => {
                        panic!("Unexpected request {:?}: {:?}", target, req)
                    }
                }
            };

            (Self { events }, closure)
        }

        fn take_events(&self) -> Vec<RcsEvent> {
            self.events.lock().unwrap().drain(..).collect()
        }
    }

    #[derive(Debug, PartialEq)]
    enum RcsEvent {
        ReverseTcp,
    }

    #[derive(Default)]
    struct TestEventHandlerProvider;

    #[async_trait::async_trait(?Send)]
    impl EventHandlerProvider for TestEventHandlerProvider {
        async fn setup_event_handlers(&mut self, cx: Context, inner: Arc<RwLock<RepoInner>>) {
            let handler =
                TargetEventHandler::new(cx, inner, Target::new_named(TARGET_NODENAME.to_string()));
            handler.on_event(TargetEvent::RcsActivated).await.unwrap();
        }
    }

    fn pm_repo_spec() -> RepositorySpec {
        let path = fs::canonicalize(EMPTY_REPO_PATH).unwrap();
        RepositorySpec::Pm { path: path.try_into().unwrap() }
    }

    fn filesystem_repo_spec() -> RepositorySpec {
        let repo = fs::canonicalize(EMPTY_REPO_PATH).unwrap();
        let metadata_repo_path = repo.join("repository");
        let blob_repo_path = metadata_repo_path.join("blobs");
        RepositorySpec::FileSystem {
            metadata_repo_path: metadata_repo_path.try_into().unwrap(),
            blob_repo_path: blob_repo_path.try_into().unwrap(),
        }
    }

    async fn add_repo(proxy: &bridge::RepositoryRegistryProxy, repo_name: &str) {
        let spec = pm_repo_spec();
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

    lazy_static::lazy_static! {
        static ref TEST_LOCK: Arc<Mutex<()>> = Arc::new(Mutex::new(()));
    }

    // FIXME(http://fxbug.dev/80740): Rust tests on host use panic=unwind, which causes all the
    // tests to run in the same process. Unfortunately ffx_config is global, and so each of these
    // tests could step on each others ffx_config entries if run in parallel. To avoid this, we
    // will:
    //
    // * use a global lock to make sure each test runs sequentially
    // * clear out the config keys before we run each test to make sure state isn't leaked across
    //   tests.
    fn run_test<F: Future>(fut: F) -> F::Output {
        let _guard = TEST_LOCK.lock().unwrap();

        let _ = simplelog::SimpleLogger::init(
            simplelog::LevelFilter::Debug,
            simplelog::Config::default(),
        );

        fuchsia_async::TestExecutor::new().unwrap().run_singlethreaded(async move {
            ffx_config::init(&[], None, None).unwrap();

            // Since ffx_config is global, it's possible to leave behind entries
            // across tests. Let's clean them up.
            let _ = ffx_config::remove("repository.server.mode").await;
            let _ = ffx_config::remove("repository.server.listen").await;
            let _ = pkg::config::remove_repository(REPO_NAME).await;
            let _ = pkg::config::remove_registration(REPO_NAME, TARGET_NODENAME).await;

            // Most tests want the server to be running.
            ffx_config::set(("repository.server.mode", ConfigLevel::User), "ffx".into())
                .await
                .unwrap();

            // Repo will automatically start a server, so make sure it picks a random local port.
            let addr: SocketAddr = (Ipv4Addr::LOCALHOST, 0).into();
            ffx_config::set(
                ("repository.server.listen", ConfigLevel::User),
                addr.to_string().into(),
            )
            .await
            .unwrap();

            fut.await
        })
    }

    #[test]
    fn test_load_from_config_empty() {
        run_test(async {
            // Initialize a simple repository.
            ffx_config::set(("repository", ConfigLevel::User), serde_json::json!({}))
                .await
                .unwrap();

            let daemon = FakeDaemonBuilder::new()
                .register_fidl_protocol::<Repo<TestEventHandlerProvider>>()
                .build();
            let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;

            assert_eq!(get_repositories(&proxy).await, vec![]);
            assert_eq!(get_target_registrations(&proxy).await, vec![]);
        })
    }

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
                            "type": "pm",
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
                    },
                    "server": {
                        "mode": "ffx",
                        "listen": SocketAddr::from((Ipv4Addr::LOCALHOST, 0)).to_string(),
                    },
                }),
            )
            .await
            .unwrap();

            let repo = Rc::new(RefCell::new(Repo::<TestEventHandlerProvider>::default()));
            let (_fake_rcs, fake_rcs_closure) = FakeRcs::new();
            let (fake_repo_manager, fake_repo_manager_closure) = FakeRepositoryManager::new();
            let (fake_engine, fake_engine_closure) = FakeEngine::new();

            let daemon = FakeDaemonBuilder::new()
                .rcs_handler(fake_rcs_closure)
                .register_instanced_protocol_closure::<RepositoryManagerMarker, _>(
                    fake_repo_manager_closure,
                )
                .register_instanced_protocol_closure::<EngineMarker, _>(fake_engine_closure)
                .inject_fidl_protocol(Rc::clone(&repo))
                .target(bridge::TargetInfo {
                    nodename: Some(TARGET_NODENAME.to_string()),
                    ssh_host_address: Some(bridge::SshHostAddrInfo {
                        address: HOST_ADDR.to_string(),
                    }),
                    ..bridge::TargetInfo::EMPTY
                })
                .build();

            let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;

            // Make sure we set up the repository and rewrite rules on the device.
            assert_eq!(
                fake_repo_manager.take_events(),
                vec![RepositoryManagerEvent::Add { repo: test_repo_config(&repo).await }],
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
                    spec: bridge::RepositorySpec::Pm(bridge::PmRepositorySpec {
                        path: Some(repo_path.clone()),
                        ..bridge::PmRepositorySpec::EMPTY
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

    #[test]
    fn test_add_remove() {
        run_test(async {
            let repo = Rc::new(RefCell::new(Repo::<TestEventHandlerProvider>::default()));
            let (fake_rcs, fake_rcs_closure) = FakeRcs::new();

            let daemon = FakeDaemonBuilder::new()
                .rcs_handler(fake_rcs_closure)
                .inject_fidl_protocol(Rc::clone(&repo))
                .build();

            let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;
            let spec = bridge::RepositorySpec::Pm(bridge::PmRepositorySpec {
                path: Some(EMPTY_REPO_PATH.to_owned()),
                ..bridge::PmRepositorySpec::EMPTY
            });

            // Initially no server should be running.
            {
                let inner = Arc::clone(&repo.borrow().inner);
                assert_matches!(inner.read().await.server, ServerState::Stopped(_));
            }

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

            // Adding a repository should start the server.
            {
                let inner = Arc::clone(&repo.borrow().inner);
                assert_matches!(inner.read().await.server, ServerState::Running(_));
            }

            // Adding a repository should not create a tunnel, since we haven't registered the
            // repository on a device.
            assert_eq!(fake_rcs.take_events(), vec![]);

            assert!(proxy.remove_repository(REPO_NAME).await.unwrap());

            // Make sure the repository was removed.
            assert_eq!(get_repositories(&proxy).await, vec![]);
        })
    }

    #[test]
    fn test_removing_repo_also_deregisters_from_target() {
        run_test(async {
            let repo = Rc::new(RefCell::new(Repo::<TestEventHandlerProvider>::default()));
            let (fake_repo_manager, fake_repo_manager_closure) = FakeRepositoryManager::new();
            let (fake_engine, fake_engine_closure) = FakeEngine::new();
            let (_fake_rcs, fake_rcs_closure) = FakeRcs::new();

            let daemon = FakeDaemonBuilder::new()
                .rcs_handler(fake_rcs_closure)
                .register_instanced_protocol_closure::<RepositoryManagerMarker, _>(
                    fake_repo_manager_closure,
                )
                .register_instanced_protocol_closure::<EngineMarker, _>(fake_engine_closure)
                .inject_fidl_protocol(Rc::clone(&repo))
                .target(bridge::TargetInfo {
                    nodename: Some(TARGET_NODENAME.to_string()),
                    ssh_host_address: Some(bridge::SshHostAddrInfo {
                        address: HOST_ADDR.to_string(),
                    }),
                    ..bridge::TargetInfo::EMPTY
                })
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
                vec![RepositoryManagerEvent::Add { repo: test_repo_config(&repo).await }]
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

    #[test]
    fn test_add_register_deregister() {
        run_test(async {
            let repo = Rc::new(RefCell::new(Repo::<TestEventHandlerProvider>::default()));
            let (fake_repo_manager, fake_repo_manager_closure) = FakeRepositoryManager::new();
            let (fake_engine, fake_engine_closure) = FakeEngine::new();
            let (fake_rcs, fake_rcs_closure) = FakeRcs::new();

            let daemon = FakeDaemonBuilder::new()
                .rcs_handler(fake_rcs_closure)
                .register_instanced_protocol_closure::<RepositoryManagerMarker, _>(
                    fake_repo_manager_closure,
                )
                .register_instanced_protocol_closure::<EngineMarker, _>(fake_engine_closure)
                .inject_fidl_protocol(Rc::clone(&repo))
                .target(bridge::TargetInfo {
                    nodename: Some(TARGET_NODENAME.to_string()),
                    ssh_host_address: Some(bridge::SshHostAddrInfo {
                        address: HOST_ADDR.to_string(),
                    }),
                    ..bridge::TargetInfo::EMPTY
                })
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
                vec![RepositoryManagerEvent::Add { repo: test_repo_config(&repo).await }]
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

            // Registering a repository should create a tunnel.
            assert_eq!(fake_rcs.take_events(), vec![RcsEvent::ReverseTcp]);

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

    async fn check_add_register_server(
        listen_addr: SocketAddr,
        ssh_host_addr: String,
        expected_repo_host: String,
    ) {
        ffx_config::set(
            ("repository.server.listen", ConfigLevel::User),
            format!("{}", listen_addr).into(),
        )
        .await
        .unwrap();

        let repo = Rc::new(RefCell::new(Repo::<TestEventHandlerProvider>::default()));
        let (fake_repo_manager, fake_repo_manager_closure) = FakeRepositoryManager::new();
        let (fake_engine, fake_engine_closure) = FakeEngine::new();
        let (_fake_rcs, fake_rcs_closure) = FakeRcs::new();

        let daemon = FakeDaemonBuilder::new()
            .rcs_handler(fake_rcs_closure)
            .register_instanced_protocol_closure::<RepositoryManagerMarker, _>(
                fake_repo_manager_closure,
            )
            .register_instanced_protocol_closure::<EngineMarker, _>(fake_engine_closure)
            .inject_fidl_protocol(Rc::clone(&repo))
            .target(bridge::TargetInfo {
                nodename: Some(TARGET_NODENAME.to_string()),
                ssh_host_address: Some(bridge::SshHostAddrInfo { address: ssh_host_addr.clone() }),
                ..bridge::TargetInfo::EMPTY
            })
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
                aliases: None,
                ..bridge::RepositoryTarget::EMPTY
            })
            .await
            .expect("communicated with proxy")
            .expect("target registration to succeed");

        // Registering the target should have set up a repository.
        let repo_config = test_repo_config_with_repo_host(&repo, Some(expected_repo_host)).await;
        assert_eq!(
            fake_repo_manager.take_events(),
            vec![RepositoryManagerEvent::Add { repo: repo_config }]
        );
    }

    #[test]
    fn test_does_not_start_server_if_server_mode_is_not_ffx() {
        run_test(async {
            ffx_config::set(("repository.server.mode", ConfigLevel::User), "pm".into())
                .await
                .unwrap();

            let repo = Rc::new(RefCell::new(Repo::<TestEventHandlerProvider>::default()));
            let (_fake_repo_manager, fake_repo_manager_closure) = FakeRepositoryManager::new();
            let (_fake_engine, fake_engine_closure) = FakeEngine::new();
            let (_fake_rcs, fake_rcs_closure) = FakeRcs::new();

            let daemon = FakeDaemonBuilder::new()
                .rcs_handler(fake_rcs_closure)
                .register_instanced_protocol_closure::<RepositoryManagerMarker, _>(
                    fake_repo_manager_closure,
                )
                .register_instanced_protocol_closure::<EngineMarker, _>(fake_engine_closure)
                .inject_fidl_protocol(Rc::clone(&repo))
                .target(bridge::TargetInfo {
                    nodename: Some(TARGET_NODENAME.to_string()),
                    ssh_host_address: Some(bridge::SshHostAddrInfo {
                        address: HOST_ADDR.to_string(),
                    }),
                    ..bridge::TargetInfo::EMPTY
                })
                .build();

            let proxy = daemon.open_proxy::<bridge::RepositoryRegistryMarker>().await;

            add_repo(&proxy, REPO_NAME).await;

            let err = proxy
                .register_target(bridge::RepositoryTarget {
                    repo_name: Some(REPO_NAME.to_string()),
                    target_identifier: Some(TARGET_NODENAME.to_string()),
                    storage_type: Some(bridge::RepositoryStorageType::Ephemeral),
                    aliases: None,
                    ..bridge::RepositoryTarget::EMPTY
                })
                .await
                .expect("communicated with proxy")
                .unwrap_err();

            assert_eq!(err, bridge::RepositoryError::ServerNotRunning);
        })
    }

    #[test]
    fn test_add_register_server_loopback_ipv4() {
        run_test(async {
            check_add_register_server(
                (Ipv4Addr::LOCALHOST, 0).into(),
                Ipv4Addr::LOCALHOST.to_string(),
                Ipv4Addr::LOCALHOST.to_string(),
            )
            .await
        })
    }

    #[test]
    fn test_add_register_server_loopback_ipv6() {
        run_test(async {
            check_add_register_server(
                (Ipv6Addr::LOCALHOST, 0).into(),
                Ipv6Addr::LOCALHOST.to_string(),
                format!("[{}]", Ipv6Addr::LOCALHOST),
            )
            .await
        })
    }

    #[test]
    fn test_add_register_server_non_loopback_ipv4() {
        run_test(async {
            check_add_register_server(
                (Ipv4Addr::UNSPECIFIED, 0).into(),
                Ipv4Addr::UNSPECIFIED.to_string(),
                Ipv4Addr::UNSPECIFIED.to_string(),
            )
            .await
        })
    }

    #[test]
    fn test_add_register_server_non_loopback_ipv6() {
        run_test(async {
            check_add_register_server(
                (Ipv6Addr::UNSPECIFIED, 0).into(),
                Ipv6Addr::UNSPECIFIED.to_string(),
                format!("[{}]", Ipv6Addr::UNSPECIFIED),
            )
            .await
        })
    }

    #[test]
    fn test_add_register_server_non_loopback_ipv6_with_scope() {
        run_test(async {
            check_add_register_server(
                (Ipv6Addr::UNSPECIFIED, 0).into(),
                format!("{}%eth1", Ipv6Addr::UNSPECIFIED),
                format!("[{}%25eth1]", Ipv6Addr::UNSPECIFIED),
            )
            .await
        })
    }

    #[test]
    fn test_register_deduplicates_rules() {
        run_test(async {
            let (_fake_rcs, fake_rcs_closure) = FakeRcs::new();
            let (_fake_repo_manager, fake_repo_manager_closure) = FakeRepositoryManager::new();
            let (fake_engine, fake_engine_closure) = FakeEngine::with_rules(vec![
                rule!("fuchsia.com" => REPO_NAME, "/" => "/"),
                rule!("fuchsia.com" => "example.com", "/" => "/"),
                rule!("fuchsia.com" => "example.com", "/" => "/"),
                rule!("fuchsia.com" => "mycorp.com", "/" => "/"),
            ]);

            let daemon = FakeDaemonBuilder::new()
                .rcs_handler(fake_rcs_closure)
                .register_instanced_protocol_closure::<RepositoryManagerMarker, _>(
                    fake_repo_manager_closure,
                )
                .register_instanced_protocol_closure::<EngineMarker, _>(fake_engine_closure)
                .register_fidl_protocol::<Repo<TestEventHandlerProvider>>()
                .target(bridge::TargetInfo {
                    nodename: Some(TARGET_NODENAME.to_string()),
                    ssh_host_address: Some(bridge::SshHostAddrInfo {
                        address: HOST_ADDR.to_string(),
                    }),
                    ..bridge::TargetInfo::EMPTY
                })
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

    #[test]
    fn test_remove_default_repository() {
        run_test(async {
            let (_fake_repo_manager, fake_repo_manager_closure) = FakeRepositoryManager::new();

            let daemon = FakeDaemonBuilder::new()
                .register_instanced_protocol_closure::<RepositoryManagerMarker, _>(
                    fake_repo_manager_closure,
                )
                .register_fidl_protocol::<Repo<TestEventHandlerProvider>>()
                .target(bridge::TargetInfo {
                    nodename: Some(TARGET_NODENAME.to_string()),
                    ssh_host_address: Some(bridge::SshHostAddrInfo {
                        address: HOST_ADDR.to_string(),
                    }),
                    ..bridge::TargetInfo::EMPTY
                })
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

    #[test]
    fn test_add_register_default_target() {
        run_test(async {
            let repo = Rc::new(RefCell::new(Repo::<TestEventHandlerProvider>::default()));
            let (_fake_rcs, fake_rcs_closure) = FakeRcs::new();
            let (fake_repo_manager, fake_repo_manager_closure) = FakeRepositoryManager::new();
            let (fake_engine, fake_engine_closure) = FakeEngine::new();

            let daemon = FakeDaemonBuilder::new()
                .rcs_handler(fake_rcs_closure)
                .register_instanced_protocol_closure::<RepositoryManagerMarker, _>(
                    fake_repo_manager_closure,
                )
                .register_instanced_protocol_closure::<EngineMarker, _>(fake_engine_closure)
                .inject_fidl_protocol(Rc::clone(&repo))
                .target(bridge::TargetInfo {
                    nodename: Some(TARGET_NODENAME.to_string()),
                    ssh_host_address: Some(bridge::SshHostAddrInfo {
                        address: HOST_ADDR.to_string(),
                    }),
                    ..bridge::TargetInfo::EMPTY
                })
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
                vec![RepositoryManagerEvent::Add { repo: test_repo_config(&repo).await }]
            );

            // We didn't set up any aliases.
            assert_eq!(fake_engine.take_events(), vec![]);
        });
    }

    #[test]
    fn test_add_register_empty_aliases() {
        run_test(async {
            let repo = Rc::new(RefCell::new(Repo::<TestEventHandlerProvider>::default()));
            let (_fake_rcs, fake_rcs_closure) = FakeRcs::new();
            let (fake_repo_manager, fake_repo_manager_closure) = FakeRepositoryManager::new();
            let (fake_engine, fake_engine_closure) = FakeEngine::new();

            let daemon = FakeDaemonBuilder::new()
                .rcs_handler(fake_rcs_closure)
                .register_instanced_protocol_closure::<RepositoryManagerMarker, _>(
                    fake_repo_manager_closure,
                )
                .register_instanced_protocol_closure::<EngineMarker, _>(fake_engine_closure)
                .inject_fidl_protocol(Rc::clone(&repo))
                .target(bridge::TargetInfo {
                    nodename: Some(TARGET_NODENAME.to_string()),
                    ssh_host_address: Some(bridge::SshHostAddrInfo {
                        address: HOST_ADDR.to_string(),
                    }),
                    ..bridge::TargetInfo::EMPTY
                })
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
                vec![RepositoryManagerEvent::Add { repo: test_repo_config(&repo).await }]
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

    #[test]
    fn test_add_register_none_aliases() {
        run_test(async {
            let repo = Rc::new(RefCell::new(Repo::<TestEventHandlerProvider>::default()));
            let (fake_repo_manager, fake_repo_manager_closure) = FakeRepositoryManager::new();
            let (fake_engine, fake_engine_closure) = FakeEngine::new();
            let (_fake_rcs, fake_rcs_closure) = FakeRcs::new();

            let daemon = FakeDaemonBuilder::new()
                .rcs_handler(fake_rcs_closure)
                .register_instanced_protocol_closure::<RepositoryManagerMarker, _>(
                    fake_repo_manager_closure,
                )
                .register_instanced_protocol_closure::<EngineMarker, _>(fake_engine_closure)
                .inject_fidl_protocol(Rc::clone(&repo))
                .target(bridge::TargetInfo {
                    nodename: Some(TARGET_NODENAME.to_string()),
                    ssh_host_address: Some(bridge::SshHostAddrInfo {
                        address: HOST_ADDR.to_string(),
                    }),
                    ..bridge::TargetInfo::EMPTY
                })
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
                vec![RepositoryManagerEvent::Add { repo: test_repo_config(&repo).await }],
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

    #[test]
    fn test_add_register_repo_manager_error() {
        run_test(async {
            let repo = Rc::new(RefCell::new(Repo::<TestEventHandlerProvider>::default()));
            let (erroring_repo_manager, erroring_repo_manager_closure) =
                ErroringRepositoryManager::new();
            let (fake_engine, fake_engine_closure) = FakeEngine::new();

            let daemon = FakeDaemonBuilder::new()
                .register_instanced_protocol_closure::<RepositoryManagerMarker, _>(
                    erroring_repo_manager_closure,
                )
                .register_instanced_protocol_closure::<EngineMarker, _>(fake_engine_closure)
                .inject_fidl_protocol(Rc::clone(&repo))
                .target(bridge::TargetInfo {
                    nodename: Some(TARGET_NODENAME.to_string()),
                    ssh_host_address: Some(bridge::SshHostAddrInfo {
                        address: HOST_ADDR.to_string(),
                    }),
                    ..bridge::TargetInfo::EMPTY
                })
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

            // Make sure we tried to add the repository.
            assert_eq!(
                erroring_repo_manager.take_events(),
                vec![RepositoryManagerEvent::Add { repo: test_repo_config(&repo).await }],
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

    #[test]
    fn test_register_non_existent_repo() {
        run_test(async {
            let repo = Rc::new(RefCell::new(Repo::<TestEventHandlerProvider>::default()));
            let (erroring_repo_manager, erroring_repo_manager_closure) =
                ErroringRepositoryManager::new();
            let (fake_engine, fake_engine_closure) = FakeEngine::new();

            let daemon = FakeDaemonBuilder::new()
                .register_instanced_protocol_closure::<RepositoryManagerMarker, _>(
                    erroring_repo_manager_closure,
                )
                .register_instanced_protocol_closure::<EngineMarker, _>(fake_engine_closure)
                .inject_fidl_protocol(Rc::clone(&repo))
                .target(bridge::TargetInfo {
                    nodename: Some(TARGET_NODENAME.to_string()),
                    ssh_host_address: Some(bridge::SshHostAddrInfo {
                        address: HOST_ADDR.to_string(),
                    }),
                    ..bridge::TargetInfo::EMPTY
                })
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
            assert_eq!(erroring_repo_manager.take_events(), vec![]);
            assert_eq!(fake_engine.take_events(), vec![]);
        })
    }

    #[test]
    fn test_deregister_non_existent_repo() {
        run_test(async {
            let repo = Rc::new(RefCell::new(Repo::<TestEventHandlerProvider>::default()));
            let (erroring_repo_manager, erroring_repo_manager_closure) =
                ErroringRepositoryManager::new();
            let (fake_engine, fake_engine_closure) = FakeEngine::new();

            let daemon = FakeDaemonBuilder::new()
                .register_instanced_protocol_closure::<RepositoryManagerMarker, _>(
                    erroring_repo_manager_closure,
                )
                .register_instanced_protocol_closure::<EngineMarker, _>(fake_engine_closure)
                .inject_fidl_protocol(Rc::clone(&repo))
                .target(bridge::TargetInfo {
                    nodename: Some(TARGET_NODENAME.to_string()),
                    ssh_host_address: Some(bridge::SshHostAddrInfo {
                        address: HOST_ADDR.to_string(),
                    }),
                    ..bridge::TargetInfo::EMPTY
                })
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
            assert_eq!(erroring_repo_manager.take_events(), vec![]);
            assert_eq!(fake_engine.take_events(), vec![]);
        });
    }

    #[test]
    fn test_build_matcher_nodename() {
        assert_eq!(
            DaemonEventHandler::build_matcher(TargetInfo {
                nodename: Some(TARGET_NODENAME.to_string()),
                ..TargetInfo::default()
            }),
            Some(TARGET_NODENAME.to_string())
        );

        assert_eq!(
            DaemonEventHandler::build_matcher(TargetInfo {
                nodename: Some(TARGET_NODENAME.to_string()),
                addresses: vec![TargetAddr::new("[fe80::1%1000]:0").unwrap()],
                ..TargetInfo::default()
            }),
            Some(TARGET_NODENAME.to_string())
        )
    }

    #[test]
    fn test_build_matcher_missing_nodename_no_port() {
        assert_eq!(
            DaemonEventHandler::build_matcher(TargetInfo {
                addresses: vec![TargetAddr::new("[fe80::1%1000]:0").unwrap()],
                ..TargetInfo::default()
            }),
            Some("fe80::1%1000".to_string())
        )
    }

    #[test]
    fn test_build_matcher_missing_nodename_with_port() {
        assert_eq!(
            DaemonEventHandler::build_matcher(TargetInfo {
                addresses: vec![TargetAddr::new("[fe80::1%1000]:0").unwrap()],
                ssh_port: Some(9182),
                ..TargetInfo::default()
            }),
            Some("[fe80::1%1000]:9182".to_string())
        )
    }

    #[test]
    fn test_create_repo_port_loopback() {
        for (listen_addr, expected) in [
            ((Ipv4Addr::LOCALHOST, 1234).into(), "127.0.0.1:1234"),
            ((Ipv6Addr::LOCALHOST, 1234).into(), "[::1]:1234"),
        ] {
            // The host address should be ignored, but lets confirm it.
            for host_addr in
                ["1.2.3.4", "fe80::111:2222:3333:444:1234", "fe80::111:2222:3333:444:1234%ethxc2"]
            {
                assert_eq!(
                    create_repo_host(
                        listen_addr,
                        bridge::SshHostAddrInfo { address: host_addr.into() },
                    ),
                    (true, expected.to_string()),
                );
            }
        }
    }

    #[test]
    fn test_create_repo_port_non_loopback() {
        for listen_addr in
            [(Ipv4Addr::UNSPECIFIED, 1234).into(), (Ipv6Addr::UNSPECIFIED, 1234).into()]
        {
            for (host_addr, expected) in [
                ("1.2.3.4", "1.2.3.4:1234"),
                ("fe80::111:2222:3333:444", "[fe80::111:2222:3333:444]:1234"),
                ("fe80::111:2222:3333:444%ethxc2", "[fe80::111:2222:3333:444%25ethxc2]:1234"),
            ] {
                assert_eq!(
                    create_repo_host(
                        listen_addr,
                        bridge::SshHostAddrInfo { address: host_addr.into() },
                    ),
                    (false, expected.to_string()),
                );
            }
        }
    }

    #[test]
    fn test_pm_repo_spec_to_backend() {
        run_test(async {
            let repo = RepoInner::new();
            let spec = pm_repo_spec();
            let backend = repo_spec_to_backend(&spec, &repo).await.unwrap();
            assert_eq!(spec, backend.spec());
        })
    }

    #[test]
    fn test_filesystem_repo_spec_to_backend() {
        run_test(async {
            let repo = RepoInner::new();
            let spec = filesystem_repo_spec();
            let backend = repo_spec_to_backend(&spec, &repo).await.unwrap();
            assert_eq!(spec, backend.spec());
        })
    }

    #[test]
    fn test_http_repo_spec_to_backend() {
        run_test(async {
            // Serve the empty repository
            let repo_path = fs::canonicalize(EMPTY_REPO_PATH).unwrap();
            let pm_backend = PmRepository::new(repo_path.try_into().unwrap());

            let pm_repo = Repository::new("tuf", Box::new(pm_backend)).await.unwrap();
            let manager = RepositoryManager::new();
            manager.add(Arc::new(pm_repo));

            let addr = (Ipv4Addr::LOCALHOST, 0).into();
            let (server_fut, _, server) =
                RepositoryServer::builder(addr, Arc::clone(&manager)).start().await.unwrap();

            // Run the server in the background.
            let _task = fasync::Task::local(server_fut);

            let http_spec = RepositorySpec::Http {
                metadata_repo_url: server.local_url() + "/tuf/",
                blob_repo_url: server.local_url() + "/tuf/blobs/",
            };

            let repo = RepoInner::new();
            let http_backend = repo_spec_to_backend(&http_spec, &repo).await.unwrap();
            assert_eq!(http_spec, http_backend.spec());

            // It rejects invalid urls.
            assert_matches!(
                repo_spec_to_backend(
                    &RepositorySpec::Http {
                        metadata_repo_url: "hello there".to_string(),
                        blob_repo_url: server.local_url() + "/tuf/blobs",
                    },
                    &repo
                )
                .await,
                Err(bridge::RepositoryError::InvalidUrl)
            );

            assert_matches!(
                repo_spec_to_backend(
                    &RepositorySpec::Http {
                        metadata_repo_url: server.local_url() + "/tuf",
                        blob_repo_url: "hello there".to_string(),
                    },
                    &repo
                )
                .await,
                Err(bridge::RepositoryError::InvalidUrl)
            );
        })
    }
}
