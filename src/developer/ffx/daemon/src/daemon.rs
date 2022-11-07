// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Result},
    ascendd::Ascendd,
    async_trait::async_trait,
    errors::ffx_error,
    ffx_build_version::build_info,
    ffx_config::EnvironmentContext,
    ffx_daemon_core::events::{self, EventHandler},
    ffx_daemon_events::{
        DaemonEvent, TargetConnectionState, TargetEvent, TargetInfo, WireTrafficType,
    },
    ffx_daemon_protocols::create_protocol_register_map,
    ffx_daemon_target::manual_targets::{Config, ManualTargets},
    ffx_daemon_target::target::Target,
    ffx_daemon_target::target_collection::TargetCollection,
    ffx_daemon_target::zedboot::zedboot_discovery,
    ffx_metrics::{add_daemon_launch_event, add_daemon_metrics_event},
    ffx_stream_util::TryStreamUtilExt,
    fidl::{endpoints::ClientEnd, prelude::*},
    fidl_fuchsia_developer_ffx::{
        self as ffx, DaemonError, DaemonMarker, DaemonRequest, DaemonRequestStream,
        RepositoryRegistryMarker, TargetCollectionMarker, VersionInfo,
    },
    fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlProxy},
    fidl_fuchsia_overnet::Peer,
    fidl_fuchsia_overnet::{ServiceProviderRequest, ServiceProviderRequestStream},
    fidl_fuchsia_overnet_protocol::NodeId,
    fuchsia_async::{Task, TimeoutExt, Timer},
    futures::{
        channel::{mpsc, oneshot},
        executor::block_on,
        prelude::*,
    },
    hoist::{Hoist, OvernetInstance},
    notify::{RecommendedWatcher, RecursiveMode, Watcher},
    protocols::{DaemonProtocolProvider, ProtocolError, ProtocolRegister},
    rcs::RcsConnection,
    std::cell::Cell,
    std::collections::HashSet,
    std::hash::{Hash, Hasher},
    std::net::SocketAddr,
    std::path::PathBuf,
    std::rc::Rc,
    std::time::{Duration, Instant},
};

// Daemon

// This is just for mocking config values for unit testing.
#[async_trait(?Send)]
trait ConfigReader: Send + Sync {
    async fn get(&self, q: &str) -> Result<Option<String>>;
}

#[derive(Default)]
struct DefaultConfigReader {}

#[async_trait(?Send)]
impl ConfigReader for DefaultConfigReader {
    async fn get(&self, q: &str) -> Result<Option<String>> {
        Ok(ffx_config::get(q).await?)
    }
}

pub struct DaemonEventHandler {
    hoist: Hoist,
    target_collection: Rc<TargetCollection>,
}

impl DaemonEventHandler {
    fn new(hoist: Hoist, target_collection: Rc<TargetCollection>) -> Self {
        Self { hoist, target_collection }
    }

    #[tracing::instrument(level = "info", skip(self))]
    async fn handle_overnet_peer(&self, node_id: u64) {
        let rcs = match RcsConnection::new(self.hoist.clone(), &mut NodeId { id: node_id }) {
            Ok(rcs) => rcs,
            Err(e) => {
                tracing::error!(
                    "Target from Overnet {} failed to connect to RCS: {:?}",
                    node_id,
                    e
                );
                return;
            }
        };

        let target = match Target::from_rcs_connection(rcs).await {
            Ok(target) => target,
            Err(err) => {
                tracing::error!(
                    "Target from Overnet {} could not be identified: {:?}",
                    node_id,
                    err
                );
                return;
            }
        };

        tracing::trace!("Target from Overnet {} is {}", node_id, target.nodename_str());
        let target = self.target_collection.merge_insert(target);
        target.run_logger();
    }

    #[tracing::instrument(level = "info", skip(self))]
    async fn handle_overnet_peer_lost(&self, node_id: u64) {
        if let Some(target) = self
            .target_collection
            .targets()
            .iter()
            .find(|target| target.overnet_node_id() == Some(node_id))
        {
            target.disconnect();
        }
    }

    fn handle_fastboot(&self, t: TargetInfo) {
        tracing::trace!(
            "Found new target via fastboot: {}",
            t.nodename.clone().unwrap_or("<unknown>".to_string())
        );
        let target = self.target_collection.merge_insert(Target::from_target_info(t.into()));
        target.update_connection_state(|s| match s {
            TargetConnectionState::Disconnected | TargetConnectionState::Fastboot(_) => {
                TargetConnectionState::Fastboot(Instant::now())
            }
            _ => s,
        });
    }

    async fn handle_zedboot(&self, t: TargetInfo) {
        tracing::trace!(
            "Found new target via zedboot: {}",
            t.nodename.clone().unwrap_or("<unknown>".to_string())
        );
        let target = self.target_collection.merge_insert(Target::from_netsvc_target_info(t.into()));
        target.update_connection_state(|s| match s {
            TargetConnectionState::Disconnected | TargetConnectionState::Zedboot(_) => {
                TargetConnectionState::Zedboot(Instant::now())
            }
            _ => s,
        });
    }
}

#[async_trait(?Send)]
impl DaemonProtocolProvider for Daemon {
    async fn open_protocol(&self, protocol_name: String) -> Result<fidl::Channel> {
        let (server, client) = fidl::Channel::create().context("creating zx channel")?;
        self.protocol_register
            .open(
                protocol_name,
                protocols::Context::new(self.clone()),
                fidl::AsyncChannel::from_channel(server)?,
            )
            .await?;
        Ok(client)
    }

    async fn open_target_proxy(
        &self,
        target_identifier: Option<String>,
        protocol_selector: fidl_fuchsia_diagnostics::Selector,
    ) -> Result<fidl::Channel> {
        let (_, channel) =
            self.open_target_proxy_with_info(target_identifier, protocol_selector).await?;
        Ok(channel)
    }

    async fn get_target_event_queue(
        &self,
        target_identifier: Option<String>,
    ) -> Result<(Rc<Target>, events::Queue<TargetEvent>)> {
        let target = self
            .get_target(target_identifier)
            .await
            .map_err(|e| anyhow!("{:#?}", e))
            .context("getting default target")?;
        target.run_host_pipe();
        let events = target.events.clone();
        Ok((target, events))
    }

    async fn open_target_proxy_with_info(
        &self,
        target_identifier: Option<String>,
        protocol_selector: fidl_fuchsia_diagnostics::Selector,
    ) -> Result<(ffx::TargetInfo, fidl::Channel)> {
        let target = self.get_rcs_ready_target(target_identifier).await?;
        let rcs = target
            .rcs()
            .ok_or(anyhow!("rcs disconnected after event fired"))
            .context("getting rcs instance")?;
        let (server, client) = fidl::Channel::create().context("creating zx channel")?;

        // TODO(awdavies): Handle these errors properly so the client knows what happened.
        rcs.proxy
            .connect(protocol_selector, server)
            .await
            .context("FIDL connection")?
            .map_err(|e| anyhow!("{:#?}", e))
            .context("proxy connect")?;
        Ok((target.as_ref().into(), client))
    }

    async fn get_target_info(&self, target_identifier: Option<String>) -> Result<ffx::TargetInfo> {
        let target = self
            .get_target(target_identifier)
            .await
            .map_err(|e| anyhow!("{:#?}", e))
            .context("getting target")?;
        Ok(target.as_ref().into())
    }

    #[tracing::instrument(level = "info", skip(self))]
    async fn open_remote_control(
        &self,
        target_identifier: Option<String>,
    ) -> Result<RemoteControlProxy> {
        let target = self.get_rcs_ready_target(target_identifier).await?;
        // Ensure auto-connect has at least started.
        let mut rcs = target
            .rcs()
            .ok_or(anyhow!("rcs disconnected after event fired"))
            .context("getting rcs instance")?;
        let (proxy, remote) = fidl::endpoints::create_proxy::<RemoteControlMarker>()?;
        rcs.copy_to_channel(remote.into_channel())?;
        Ok(proxy)
    }

    async fn daemon_event_queue(&self) -> events::Queue<DaemonEvent> {
        self.event_queue.clone()
    }

    async fn get_target_collection(&self) -> Result<Rc<TargetCollection>> {
        Ok(self.target_collection.clone())
    }
}

#[async_trait(?Send)]
impl EventHandler<DaemonEvent> for DaemonEventHandler {
    async fn on_event(&self, event: DaemonEvent) -> Result<events::Status> {
        tracing::info!("! DaemonEvent::{:?}", event);

        match event {
            DaemonEvent::WireTraffic(traffic) => match traffic {
                WireTrafficType::Mdns(t) => {
                    tracing::warn!("mdns traffic fired in daemon. This is deprecated: {:?}", t);
                }
                WireTrafficType::Fastboot(t) => {
                    self.handle_fastboot(t);
                }
                WireTrafficType::Zedboot(t) => {
                    self.handle_zedboot(t).await;
                }
            },
            DaemonEvent::OvernetPeer(node_id) => {
                self.handle_overnet_peer(node_id).await;
            }
            DaemonEvent::OvernetPeerLost(node_id) => {
                self.handle_overnet_peer_lost(node_id).await;
            }
            _ => (),
        }

        // This handler is never done unless the target_collection is dropped.
        Ok(events::Status::Waiting)
    }
}

#[derive(Clone)]
/// Defines the daemon object. This is used by "ffx daemon start".
///
/// Typical usage is:
///   let mut daemon = ffx_daemon::Daemon::new(socket_path);
///   daemon.start().await
pub struct Daemon {
    // The path to the ascendd socket this daemon will bind to
    socket_path: PathBuf,
    // The event queue is a collection of subscriptions to which DaemonEvents will be published.
    event_queue: events::Queue<DaemonEvent>,
    // All the targets currently known to the daemon.
    // This may include targets the daemon has no access to.
    target_collection: Rc<TargetCollection>,
    // ascendd is the overnet daemon running on the Linux host. It manages the mesh and the
    // connections to the devices and other peers (for example, a connection to the frontend).
    // With ffx, ascendd is embedded within the ffx daemon (when ffx daemon is launched, we don’t
    // need an extra process for ascendd).
    ascendd: Rc<Cell<Option<Ascendd>>>,
    // Handles the registered FIDL protocols and associated handles. This is initialized with the
    // list of protocols defined in src/developer/ffx/daemon/protocols/BUILD.gn (the deps field in
    // ffx_protocol) using the macro generate_protocol_map in
    // src/developer/ffx/build/templates/protocols_macro.rs.jinja.
    protocol_register: ProtocolRegister,
    // All the persistent long running tasks spawned by the daemon. The tasks are standalone. That
    // means that they execute by themselves without any intervention from the daemon.
    // The purpose of this vector is to keep the reference strong count positive until the daemon is
    // dropped.
    tasks: Vec<Rc<Task<()>>>,
}

impl Daemon {
    pub fn new(socket_path: PathBuf) -> Daemon {
        let target_collection = Rc::new(TargetCollection::new());
        let event_queue = events::Queue::new(&target_collection);
        target_collection.set_event_queue(event_queue.clone());

        Self {
            socket_path,
            target_collection,
            event_queue,
            protocol_register: ProtocolRegister::new(create_protocol_register_map()),
            ascendd: Rc::new(Cell::new(None)),
            tasks: Vec::new(),
        }
    }

    pub async fn start(&mut self, hoist: &Hoist) -> Result<()> {
        let context =
            ffx_config::global_env_context().context("Discovering ffx environment context")?;
        let (quit_tx, quit_rx) = mpsc::channel(1);
        self.log_startup_info(&context).await.context("Logging startup info")?;

        self.start_protocols().await?;
        self.start_discovery(hoist).await?;
        self.start_ascendd(hoist).await?;
        let _socket_file_watcher =
            self.start_socket_watch(quit_tx.clone()).await.context("Starting socket watcher")?;
        self.start_target_expiry(Duration::from_secs(1));
        self.serve(&context, hoist, quit_tx, quit_rx).await.context("Serving clients")
    }

    async fn log_startup_info(&self, context: &EnvironmentContext) -> Result<()> {
        let pid = std::process::id();
        let buildid = context.daemon_version_string()?;
        let version_info = build_info();
        let commit_hash = version_info.commit_hash.as_deref().unwrap_or("<unknown>");
        let commit_timestamp =
            version_info.commit_timestamp.map(|t| t.to_string()).unwrap_or("<unknown>".to_owned());
        let build_version = version_info.build_version.as_deref().unwrap_or("<unknown>");

        tracing::info!(
            "Beginning daemon startup\nBuild Version: {build_version}\nCommit Timestamp: {commit_timestamp}\nCommit Hash: {commit_hash}\nBinary Build ID: {buildid}\nPID: {pid}",
        );
        add_daemon_launch_event().await;
        Ok(())
    }

    async fn start_protocols(&mut self) -> Result<()> {
        let cx = protocols::Context::new(self.clone());
        let ((), ()) = futures::future::try_join(
            self.protocol_register
                .start(RepositoryRegistryMarker::PROTOCOL_NAME.to_string(), cx.clone()),
            self.protocol_register.start(TargetCollectionMarker::PROTOCOL_NAME.to_string(), cx),
        )
        .await?;
        Ok(())
    }

    /// Awaits a target that has RCS active.
    async fn get_rcs_ready_target(&self, target_query: Option<String>) -> Result<Rc<Target>> {
        let target = self
            .get_target(target_query)
            .await
            .map_err(|e| anyhow!("{:#?}", e))
            .context("getting default target")?;
        if matches!(target.get_connection_state(), TargetConnectionState::Fastboot(_)) {
            let nodename = target.nodename().unwrap_or("<No Nodename>".to_string());
            bail!("Attempting to open RCS on a fastboot target: {}", nodename);
        }
        if matches!(target.get_connection_state(), TargetConnectionState::Zedboot(_)) {
            let nodename = target.nodename().unwrap_or("<No Nodename>".to_string());
            bail!("Attempting to connect to RCS on a zedboot target: {}", nodename);
        }
        // Ensure auto-connect has at least started.
        target.run_host_pipe();
        target
            .events
            .wait_for(None, |e| e == TargetEvent::RcsActivated)
            .await
            .context("waiting for RCS activation")?;
        Ok(target)
    }

    /// Start all discovery tasks
    async fn start_discovery(&mut self, hoist: &Hoist) -> Result<()> {
        let daemon_event_handler =
            DaemonEventHandler::new(hoist.clone(), self.target_collection.clone());
        self.event_queue.add_handler(daemon_event_handler).await;

        // TODO: these tasks could and probably should be managed by the daemon
        // instead of being detached.
        Daemon::spawn_onet_discovery(hoist, self.event_queue.clone());
        let discovery = zedboot_discovery(self.event_queue.clone()).await?;
        self.tasks.push(Rc::new(discovery));
        Ok(())
    }

    async fn start_ascendd(&mut self, hoist: &Hoist) -> Result<()> {
        // Start the ascendd socket only after we have registered our protocols.
        tracing::info!("Starting ascendd");

        let client_routing = false; // Don't route between ffx clients
        let ascendd = Ascendd::new(
            ascendd::Opt {
                sockpath: Some(self.socket_path.clone()),
                client_routing,
                ..Default::default()
            },
            &hoist,
            // TODO: this just prints serial output to stdout - ffx probably wants to take a more
            // nuanced approach here.
            blocking::Unblock::new(std::io::stdout()),
        )
        .await
        .map_err(|e| ffx_error!("Error trying to start daemon socket: {e}"))?;

        self.ascendd.replace(Some(ascendd));

        Ok(())
    }

    async fn start_socket_watch(&self, quit_tx: mpsc::Sender<()>) -> Result<RecommendedWatcher> {
        let socket_path = self.socket_path.clone();
        let socket_dir = self.socket_path.parent().context("Getting parent directory of socket")?;
        let mut watcher = RecommendedWatcher::new_immediate(move |res| {
            let mut quit_tx = quit_tx.clone();
            block_on(async {
                use notify::event::{Event, EventKind::Remove};
                match res {
                    Ok(Event { kind: Remove(_), paths, .. }) if paths.contains(&socket_path) => {
                        tracing::info!("daemon socket was deleted, triggering quit message.");
                        quit_tx.send(()).await.ok();
                    }
                    Err(e) => {
                        // if we get an error, treat that as something that should cause us to exit.
                        tracing::warn!("watch error: {e:?}");
                        quit_tx.send(()).await.ok();
                    }
                    Ok(_) => {} // just ignore any non-delete event or for any other file.
                }
            })
        })
        .context("Creating watcher")?;

        // we have to watch the directory because watching a file does weird things and only
        // half works. This seems to be a limitation of underlying libraries.
        watcher
            .watch(&socket_dir, RecursiveMode::NonRecursive)
            .context("Setting watcher context")?;

        tracing::info!(
            "Watching daemon socket file at {socket_path}, will gracefully exit if it's removed.",
            socket_path = self.socket_path.display()
        );

        Ok(watcher)
    }

    fn start_target_expiry(&mut self, frequency: Duration) {
        let target_collection = Rc::downgrade(&self.target_collection);
        self.tasks.push(Rc::new(Task::local(async move {
            loop {
                Timer::new(frequency.clone()).await;
                let manual_targets = Config::default();

                match target_collection.upgrade() {
                    Some(target_collection) => {
                        for target in target_collection.targets() {
                            // Manually-added remote targets will not be discovered by mDNS,
                            // and as a result will not have host-pipe triggered automatically
                            // by the mDNS event handler.
                            if target.is_manual() {
                                target.run_host_pipe();
                            }
                            target.expire_state();
                            if target.is_manual() && !target.is_connected() {
                                // If a manual target has been allowed to transition to the
                                // "disconnected" state, it should be removed from the collection.
                                let ssh_port = target.ssh_port();
                                for addr in target.manual_addrs() {
                                    let mut sockaddr = SocketAddr::from(addr);
                                    ssh_port.map(|p| sockaddr.set_port(p));
                                    let _ = manual_targets
                                        .remove(format!("{}", sockaddr))
                                        .await
                                        .map_err(|e| {
                                            tracing::error!(
                                                "Unable to persist ephemeral target removal: {}",
                                                e
                                            );
                                        });
                                }
                                target_collection.remove_ephemeral_target(target);
                            }
                        }
                    }
                    None => return,
                }
            }
        })))
    }

    /// get_target attempts to get the target that matches the match string if
    /// provided, otherwise the default target from the target collection.
    async fn get_target(&self, matcher: Option<String>) -> Result<Rc<Target>, DaemonError> {
        #[cfg(not(test))]
        const GET_TARGET_TIMEOUT: Duration = Duration::from_secs(8);
        #[cfg(test)]
        const GET_TARGET_TIMEOUT: Duration = Duration::from_secs(1);

        // TODO(72818): make target match timeout configurable / paramterable
        self.target_collection
            .wait_for_match(matcher)
            .on_timeout(GET_TARGET_TIMEOUT, || match self.target_collection.is_empty() {
                true => Err(DaemonError::TargetCacheEmpty),
                false => Err(DaemonError::TargetNotFound),
            })
            .await
    }

    async fn handle_requests_from_stream(
        &self,
        quit_tx: &mpsc::Sender<()>,
        stream: DaemonRequestStream,
        info: &VersionInfo,
    ) -> Result<()> {
        stream
            .map_err(|e| anyhow!("reading FIDL stream: {:#}", e))
            .try_for_each_concurrent_while_connected(None, |r| async {
                let debug_req_string = format!("{:?}", r);
                if let Err(e) = self.handle_request(quit_tx, r, info).await {
                    tracing::error!("error while handling request `{}`: {}", debug_req_string, e);
                }
                Ok(())
            })
            .await
    }

    fn spawn_onet_discovery(hoist: &Hoist, queue: events::Queue<DaemonEvent>) {
        let hoist = hoist.clone();
        fuchsia_async::Task::local(async move {
            let mut known_peers: HashSet<PeerSetElement> = Default::default();

            loop {
                let svc = match hoist.connect_as_service_consumer() {
                    Ok(svc) => svc,
                    Err(err) => {
                        tracing::info!("Overnet setup failed: {}, will retry in 1s", err);
                        Timer::new(Duration::from_secs(1)).await;
                        continue;
                    }
                };
                loop {
                    match svc.list_peers().await {
                        Ok(new_peers) => {
                            known_peers =
                                Self::handle_overnet_peers(&queue, known_peers, new_peers);
                        }
                        Err(err) => {
                            tracing::info!("Overnet peer discovery failed: {}, will retry", err);
                            Timer::new(Duration::from_secs(1)).await;
                            // break out of the peer discovery loop on error in
                            // order to reconnect, in case the error causes the
                            // overnet interface to go bad.
                            break;
                        }
                    };
                }
            }
        })
        .detach();
    }

    fn handle_overnet_peers(
        queue: &events::Queue<DaemonEvent>,
        known_peers: HashSet<PeerSetElement>,
        peers: Vec<Peer>,
    ) -> HashSet<PeerSetElement> {
        let mut new_peers: HashSet<PeerSetElement> = Default::default();
        for peer in peers {
            new_peers.insert(PeerSetElement(peer));
        }

        for peer in new_peers.difference(&known_peers) {
            let peer = &peer.0;
            let peer_has_rcs = peer
                .description
                .services
                .as_ref()
                .map(|v| v.contains(&RemoteControlMarker::PROTOCOL_NAME.to_string()))
                .unwrap_or(false);
            if peer_has_rcs {
                queue.push(DaemonEvent::OvernetPeer(peer.id.id)).unwrap_or_else(|err| {
                    tracing::warn!(
                        "Overnet discovery failed to enqueue event {:?}: {}",
                        DaemonEvent::OvernetPeer(peer.id.id),
                        err
                    );
                });
            }
        }

        for peer in known_peers.difference(&new_peers) {
            let peer = &peer.0;
            queue.push(DaemonEvent::OvernetPeerLost(peer.id.id)).unwrap_or_else(|err| {
                tracing::warn!(
                    "Overnet discovery failed to enqueue event {:?}: {}",
                    DaemonEvent::OvernetPeerLost(peer.id.id),
                    err
                );
            });
        }

        new_peers
    }

    async fn handle_request(
        &self,
        quit_tx: &mpsc::Sender<()>,
        req: DaemonRequest,
        info: &VersionInfo,
    ) -> Result<()> {
        tracing::debug!("daemon received request: {:?}", req);

        match req {
            DaemonRequest::Quit { responder } => {
                tracing::info!("Received quit request.");
                if cfg!(test) {
                    panic!("quit() should not be invoked in test code");
                }

                quit_tx.clone().send(()).await?;

                responder.send(true).context("error sending response")?;
            }
            DaemonRequest::GetVersionInfo { responder } => {
                return responder.send(info.clone()).context("sending GetVersionInfo response");
            }
            DaemonRequest::ConnectToProtocol { name, server_end, responder } => {
                let name_for_analytics = name.clone();
                match self
                    .protocol_register
                    .open(
                        name,
                        protocols::Context::new(self.clone()),
                        fidl::AsyncChannel::from_channel(server_end)?,
                    )
                    .await
                {
                    Ok(()) => responder.send(&mut Ok(())).context("fidl response")?,
                    Err(e) => {
                        tracing::error!("{}", e);
                        match e {
                            ProtocolError::NoProtocolFound(_) => {
                                responder.send(&mut Err(DaemonError::ProtocolNotFound))?
                            }
                            ProtocolError::StreamOpenError(_) => {
                                responder.send(&mut Err(DaemonError::ProtocolOpenError))?
                            }
                            ProtocolError::BadRegisterState(_)
                            | ProtocolError::DuplicateTaskId(..) => {
                                responder.send(&mut Err(DaemonError::BadProtocolRegisterState))?
                            }
                        }
                    }
                }
                add_daemon_metrics_event(
                    format!("connect_to_protocol: {}", &name_for_analytics).as_str(),
                )
                .await;
            }
        }

        Ok(())
    }

    async fn serve(
        &self,
        context: &EnvironmentContext,
        hoist: &Hoist,
        quit_tx: mpsc::Sender<()>,
        mut quit_rx: mpsc::Receiver<()>,
    ) -> Result<()> {
        let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
        let chan = fidl::AsyncChannel::from_channel(s).context("failed to make async channel")?;
        let mut stream = ServiceProviderRequestStream::from_channel(chan);

        let mut info = build_info();
        info.build_id = Some(context.daemon_version_string()?);
        tracing::info!("Starting daemon overnet server");
        hoist.publish_service(DaemonMarker::PROTOCOL_NAME, ClientEnd::new(p))?;

        tracing::info!("Starting daemon serve loop");
        let (break_loop_tx, mut break_loop_rx) = oneshot::channel();
        let mut break_loop_tx = Some(break_loop_tx);

        loop {
            futures::select! {
                req = stream.try_next() => match req.context("error running protocol provider server")? {
                    Some(ServiceProviderRequest::ConnectToService {
                        chan,
                        info: _,
                        control_handle: _control_handle,
                    }) =>
                    {
                        tracing::trace!("Received protocol request for protocol");
                        let chan =
                            fidl::AsyncChannel::from_channel(chan).context("failed to make async channel")?;
                        let daemon_clone = self.clone();
                        let mut quit_tx = quit_tx.clone();
                        let info = info.clone();
                        Task::local(async move {
                            if let Err(err) = daemon_clone.handle_requests_from_stream(&quit_tx, DaemonRequestStream::from_channel(chan), &info).await {
                                tracing::error!("error handling request: {:?}", err);
                                quit_tx.send(()).await.expect("Failed to gracefully send quit message, aborting.");
                            }
                        })
                        .detach();
                    },
                    o => {
                        tracing::warn!("Received unknown message or no message on provider server: {o:?}");
                        break;
                    }
                },
                _ = quit_rx.next() => {
                    if let Some(break_loop_tx) = break_loop_tx.take() {
                        tracing::info!("Starting graceful shutdown of daemon socket");

                        match std::fs::remove_file(self.socket_path.clone()) {
                            Ok(()) => {}
                            Err(e) => tracing::error!("failed to remove socket file: {}", e),
                        }

                        self.protocol_register
                            .shutdown(protocols::Context::new(self.clone()))
                            .await
                            .unwrap_or_else(|e| {
                                tracing::error!("shutting down protocol register: {:?}", e)
                            });

                        add_daemon_metrics_event("quit").await;

                        // It is desirable for the client to receive an ACK for the quit
                        // request. As Overnet has a potentially complicated routing
                        // path, it is tricky to implement some notion of a bounded
                        // "flush" for this response, however in practice it is only
                        // necessary here to wait long enough for the message to likely
                        // leave the local process before exiting. Enqueue a detached
                        // timer to shut down the daemon before sending the response.
                        // This is detached because once the client receives the
                        // response, the client will disconnect it's socket. If the
                        // local reactor observes this disconnection before the timer
                        // expires, an in-line timer wait would never fire, and the
                        // daemon would never exit.
                        Task::local(async move {
                            Timer::new(std::time::Duration::from_millis(20)).await;
                            break_loop_tx.send(()).expect("failed to send loop break message");
                        })
                        .detach();
                    } else {
                        tracing::trace!("Received quit message after shutdown was already initiated");
                    }
                },
                _ = break_loop_rx => {
                    tracing::info!("Breaking main daemon socket loop");
                    break;
                }
            }
        }
        tracing::info!("Graceful shutdown of daemon loop completed");
        ffx_config::logging::disable_stdio_logging();
        Ok(())
    }
}

// PeerSetElement wraps an overnet Peer object for inclusion in a Set
// or other collection reliant on Eq and HAsh, using the NodeId as the
// discriminator.
struct PeerSetElement(Peer);
impl PartialEq for PeerSetElement {
    fn eq(&self, other: &Self) -> bool {
        self.0.id == other.0.id
    }
}
impl Eq for PeerSetElement {}
impl Hash for PeerSetElement {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.0.id.hash(state);
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        addr::TargetAddr,
        assert_matches::assert_matches,
        chrono::Utc,
        ffx_daemon_target::target::TargetAddrEntry,
        ffx_daemon_target::target::TargetAddrType,
        fidl_fuchsia_developer_ffx::{DaemonMarker, DaemonProxy},
        fidl_fuchsia_developer_remotecontrol::RemoteControlMarker,
        fidl_fuchsia_overnet_protocol::PeerDescription,
        fuchsia_async::Task,
        std::cell::RefCell,
        std::collections::BTreeSet,
        std::iter::FromIterator,
        std::time::SystemTime,
    };

    fn spawn_test_daemon() -> (DaemonProxy, Daemon, Task<Result<()>>) {
        let tempdir = tempfile::tempdir().expect("Creating tempdir");
        let socket_path = tempdir.path().join("ascendd.sock");
        let d = Daemon::new(socket_path);

        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
        let (quit_tx, _quit_rx) = mpsc::channel(1);

        let d2 = d.clone();
        let task = Task::local(async move {
            d2.handle_requests_from_stream(&quit_tx, stream, &build_info()).await
        });

        (proxy, d, task)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_open_rcs_on_fastboot_error() {
        let (_proxy, daemon, _task) = spawn_test_daemon();
        let target = Target::new_with_serial("abc");
        daemon.target_collection.merge_insert(target);
        let result = daemon.open_remote_control(None).await;
        assert!(result.is_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_open_rcs_on_zedboot_error() {
        let (_proxy, daemon, _task) = spawn_test_daemon();
        let target = Target::new_with_netsvc_addrs(
            Some("abc"),
            BTreeSet::from_iter(vec![TargetAddr::new("[fe80::1%1]:22").unwrap()].into_iter()),
        );
        daemon.target_collection.merge_insert(target);
        let result = daemon.open_remote_control(None).await;
        assert!(result.is_err());
    }

    struct FakeConfigReader {
        query_expected: String,
        value: String,
    }

    #[async_trait(?Send)]
    impl ConfigReader for FakeConfigReader {
        async fn get(&self, q: &str) -> Result<Option<String>> {
            assert_eq!(q, self.query_expected);
            Ok(Some(self.value.clone()))
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_target_empty() {
        let tempdir = tempfile::tempdir().expect("Creating tempdir");
        let socket_path = tempdir.path().join("ascendd.sock");
        let d = Daemon::new(socket_path);
        let nodename = "where-is-my-hasenpfeffer";
        let t = Target::new_autoconnected(nodename);
        d.target_collection.merge_insert(t.clone());
        assert_eq!(nodename, d.get_target(None).await.unwrap().nodename().unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_target_query() {
        let tempdir = tempfile::tempdir().expect("Creating tempdir");
        let socket_path = tempdir.path().join("ascendd.sock");
        let d = Daemon::new(socket_path);
        let nodename = "where-is-my-hasenpfeffer";
        let t = Target::new_autoconnected(nodename);
        d.target_collection.merge_insert(t.clone());
        assert_eq!(
            nodename,
            d.get_target(Some(nodename.to_string())).await.unwrap().nodename().unwrap()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_target_collection_empty_error() {
        let tempdir = tempfile::tempdir().expect("Creating tempdir");
        let socket_path = tempdir.path().join("ascendd.sock");
        let d = Daemon::new(socket_path);
        assert_eq!(DaemonError::TargetCacheEmpty, d.get_target(None).await.unwrap_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_target_ambiguous() {
        let tempdir = tempfile::tempdir().expect("Creating tempdir");
        let socket_path = tempdir.path().join("ascendd.sock");
        let d = Daemon::new(socket_path);
        let t = Target::new_autoconnected("where-is-my-hasenpfeffer");
        let t2 = Target::new_autoconnected("it-is-rabbit-season");
        d.target_collection.merge_insert(t.clone());
        d.target_collection.merge_insert(t2.clone());
        assert_eq!(DaemonError::TargetAmbiguous, d.get_target(None).await.unwrap_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_expiry() {
        let tempdir = tempfile::tempdir().expect("Creating tempdir");
        let socket_path = tempdir.path().join("ascendd.sock");
        let mut daemon = Daemon::new(socket_path);
        let target = Target::new_named("goodbye-world");
        let then = Instant::now() - Duration::from_secs(10);
        target.update_connection_state(|_| TargetConnectionState::Mdns(then));
        daemon.target_collection.merge_insert(target.clone());

        assert_eq!(TargetConnectionState::Mdns(then), target.get_connection_state());

        daemon.start_target_expiry(Duration::from_millis(1));

        while target.get_connection_state() == TargetConnectionState::Mdns(then) {
            futures_lite::future::yield_now().await
        }

        assert_eq!(TargetConnectionState::Disconnected, target.get_connection_state());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_ephemeral_target_expiry() {
        let tempdir = tempfile::tempdir().expect("Creating tempdir");
        let socket_path = tempdir.path().join("ascendd.sock");
        let mut daemon = Daemon::new(socket_path);
        let expiring_target = Target::new_with_addr_entries(
            Some("goodbye-world"),
            vec![TargetAddrEntry {
                addr: TargetAddr::new("127.0.0.1:8088").unwrap(),
                timestamp: Utc::now(),
                addr_type: TargetAddrType::Manual(Some(SystemTime::now())),
            }]
            .into_iter(),
        );
        expiring_target.set_ssh_port(Some(8022));

        let persistent_target = Target::new_with_addr_entries(
            Some("i-will-stick-around"),
            vec![TargetAddrEntry {
                addr: TargetAddr::new("127.0.0.1:8089").unwrap(),
                timestamp: Utc::now(),
                addr_type: TargetAddrType::Manual(None),
            }]
            .into_iter(),
        );
        persistent_target.set_ssh_port(Some(8023));

        let then = Instant::now() - Duration::from_secs(10);
        expiring_target.update_connection_state(|_| TargetConnectionState::Mdns(then));
        persistent_target.update_connection_state(|_| TargetConnectionState::Mdns(then));

        assert!(daemon.target_collection.is_empty());

        daemon.target_collection.merge_insert(expiring_target.clone());
        daemon.target_collection.merge_insert(persistent_target.clone());

        assert_eq!(TargetConnectionState::Mdns(then), expiring_target.get_connection_state());
        assert_eq!(TargetConnectionState::Mdns(then), persistent_target.get_connection_state());

        daemon.start_target_expiry(Duration::from_millis(1));

        while expiring_target.get_connection_state() == TargetConnectionState::Mdns(then) {
            futures_lite::future::yield_now().await
        }
        while persistent_target.get_connection_state() == TargetConnectionState::Mdns(then) {
            futures_lite::future::yield_now().await
        }
        assert_eq!(TargetConnectionState::Disconnected, expiring_target.get_connection_state());
        assert_matches!(
            persistent_target.get_connection_state(),
            TargetConnectionState::Manual(None)
        );
        assert_eq!(daemon.target_collection.targets().len(), 1);
    }

    struct NullDaemonEventSynthesizer();

    #[async_trait(?Send)]
    impl events::EventSynthesizer<DaemonEvent> for NullDaemonEventSynthesizer {
        async fn synthesize_events(&self) -> Vec<DaemonEvent> {
            return Default::default();
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_handle_overnet_peers_known_peer_exclusion() {
        let queue = events::Queue::<DaemonEvent>::new(&Rc::new(NullDaemonEventSynthesizer {}));
        let mut known_peers: HashSet<PeerSetElement> = Default::default();

        let peer1 = Peer {
            description: PeerDescription {
                services: None,
                unknown_data: None,
                ..PeerDescription::EMPTY
            },
            id: NodeId { id: 1 },
            is_self: false,
        };
        let peer2 = Peer {
            description: PeerDescription {
                services: None,
                unknown_data: None,
                ..PeerDescription::EMPTY
            },
            id: NodeId { id: 2 },
            is_self: false,
        };

        let new_peers =
            Daemon::handle_overnet_peers(&queue, known_peers, vec![peer1.clone(), peer2.clone()]);
        assert!(new_peers.contains(&PeerSetElement(peer1.clone())));
        assert!(new_peers.contains(&PeerSetElement(peer2.clone())));

        known_peers = new_peers;

        let new_peers = Daemon::handle_overnet_peers(&queue, known_peers, vec![]);
        assert!(!new_peers.contains(&PeerSetElement(peer1.clone())));
        assert!(!new_peers.contains(&PeerSetElement(peer2.clone())));
    }

    struct DaemonEventRecorder {
        /// All events observed by the handler will be logged into this field.
        event_log: Rc<RefCell<Vec<DaemonEvent>>>,
    }
    #[async_trait(?Send)]
    impl EventHandler<DaemonEvent> for DaemonEventRecorder {
        async fn on_event(&self, event: DaemonEvent) -> Result<events::Status> {
            self.event_log.borrow_mut().push(event);
            Ok(events::Status::Waiting)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_handle_overnet_peer_leave_and_return() {
        let queue = events::Queue::<DaemonEvent>::new(&Rc::new(NullDaemonEventSynthesizer {}));
        let mut known_peers: HashSet<PeerSetElement> = Default::default();

        let peer1 = Peer {
            description: PeerDescription {
                services: Some(vec![RemoteControlMarker::PROTOCOL_NAME.to_string()]),
                unknown_data: None,
                ..PeerDescription::EMPTY
            },
            id: NodeId { id: 1 },
            is_self: false,
        };
        let peer2 = Peer {
            description: PeerDescription {
                services: Some(vec![RemoteControlMarker::PROTOCOL_NAME.to_string()]),
                unknown_data: None,
                ..PeerDescription::EMPTY
            },
            id: NodeId { id: 2 },
            is_self: false,
        };

        // First the targets are discovered:
        let new_peers =
            Daemon::handle_overnet_peers(&queue, known_peers, vec![peer1.clone(), peer2.clone()]);
        assert!(new_peers.contains(&PeerSetElement(peer1.clone())));
        assert!(new_peers.contains(&PeerSetElement(peer2.clone())));

        known_peers = new_peers;

        // Make a new queue so we don't get any of the historical events.
        let queue = events::Queue::<DaemonEvent>::new(&Rc::new(NullDaemonEventSynthesizer {}));
        let event_log = Rc::new(RefCell::new(Vec::<DaemonEvent>::new()));

        // Now wire up the event handler, we want to assert that we observe OvernetPeerLost events for the leaving targets.
        queue.add_handler(DaemonEventRecorder { event_log: event_log.clone() }).await;

        // Next the targets are lost:
        let new_peers = Daemon::handle_overnet_peers(&queue, known_peers, vec![]);
        assert!(!new_peers.contains(&PeerSetElement(peer1.clone())));
        assert!(!new_peers.contains(&PeerSetElement(peer2.clone())));

        let start = Instant::now();
        while event_log.borrow().len() != 2 {
            if Instant::now().duration_since(start) > Duration::from_secs(1) {
                break;
            }
            futures_lite::future::yield_now().await;
        }

        assert_eq!(event_log.borrow().len(), 2);
        assert_matches!(event_log.borrow()[0], DaemonEvent::OvernetPeerLost(_));
        assert_matches!(event_log.borrow()[1], DaemonEvent::OvernetPeerLost(_));

        known_peers = new_peers;

        assert_eq!(known_peers.len(), 0);

        // Make a new queue so we don't get any of the historical events.
        let queue = events::Queue::<DaemonEvent>::new(&Rc::new(NullDaemonEventSynthesizer {}));
        let event_log = Rc::new(RefCell::new(Vec::<DaemonEvent>::new()));

        // Now wire up the event handler, we want to assert that we observe NewTarget events for the returning targets.
        queue.add_handler(DaemonEventRecorder { event_log: event_log.clone() }).await;

        // Now the targets return:
        let new_peers =
            Daemon::handle_overnet_peers(&queue, known_peers, vec![peer1.clone(), peer2.clone()]);
        assert!(new_peers.contains(&PeerSetElement(peer1.clone())));
        assert!(new_peers.contains(&PeerSetElement(peer2.clone())));

        let start = Instant::now();
        while event_log.borrow().len() != 2 {
            if Instant::now().duration_since(start) > Duration::from_secs(1) {
                break;
            }
            futures_lite::future::yield_now().await;
        }

        // Ensure that we observed a new target event for each target that returned.
        assert_eq!(event_log.borrow().len(), 2);
        assert_matches!(event_log.borrow()[0], DaemonEvent::OvernetPeer(_));
        assert_matches!(event_log.borrow()[1], DaemonEvent::OvernetPeer(_));
    }
}
