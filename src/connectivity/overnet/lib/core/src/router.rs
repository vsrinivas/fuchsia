// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Manages peer<->peer connections and routing packets over links between nodes.

// General structure:
// A router is a collection of: - streams (each with a StreamId<LinkData>)
//                                These are streams of information flow between processes.
//                              - peers (each with a PeerId)
//                                These are other overnet instances in the overlay network.
//                                There is a client oriented and a server oriented peer per
//                                node.
//                              - links (each with a LinkId)
//                                These are connections between this instance and other
//                                instances in the mesh.
// For each node in the mesh, a routing table tracks which link on which to send data to
// that node (said link may be a third node that will be requested to forward datagrams
// on our behalf).

use crate::{
    async_quic::{AsyncConnection, AsyncQuicStreamReader, AsyncQuicStreamWriter, StreamProperties},
    diagnostics_service::run_diagostic_service_request_handler,
    framed_stream::MessageStats,
    future_help::{log_errors, Observable, Observer, PollMutex},
    handle_info::{handle_info, HandleKey, HandleType},
    labels::{ConnectionId, Endpoint, NodeId, NodeLinkId, TransferKey},
    link::{new_link, LinkReceiver, LinkRouting, LinkSender},
    link_status_updater::{run_link_status_updater, LinkStatePublisher},
    peer::Peer,
    proxy::{ProxyTransferInitiationReceiver, RemoveFromProxyTable, StreamRefSender},
    proxyable_handle::IntoProxied,
    routes::{ForwardingTable, Routes},
    security_context::{quiche_config_from_security_context, SecurityContext},
    service_map::{ListablePeer, ServiceMap},
    socket_link::run_socket_link,
};
use anyhow::{bail, format_err, Context as _, Error};
use fidl::{endpoints::ClientEnd, AsHandleRef, Channel, Handle, HandleBased, Socket, SocketOpts};
use fidl_fuchsia_overnet::{ConnectionInfo, ServiceProviderMarker};
use fidl_fuchsia_overnet_protocol::{
    ChannelHandle, LinkDiagnosticInfo, PeerConnectionDiagnosticInfo, RouteMetrics, SocketHandle,
    SocketType, StreamId, StreamRef, ZirconHandle,
};
use fuchsia_async::Task;
use futures::{future::poll_fn, lock::Mutex, prelude::*, ready};
use rand::Rng;
use std::{
    collections::{btree_map, BTreeMap, HashMap},
    convert::TryInto,
    sync::atomic::{AtomicU64, Ordering},
    sync::{Arc, Weak},
    task::{Context, Poll, Waker},
};

/// Configuration object for creating a router.
pub struct RouterOptions {
    node_id: Option<NodeId>,
    diagnostics: Option<fidl_fuchsia_overnet_protocol::Implementation>,
}

impl RouterOptions {
    /// Create with defaults.
    pub fn new() -> Self {
        RouterOptions { diagnostics: None, node_id: None }
    }

    /// Request a specific node id (if unset, one will be generated).
    pub fn set_node_id(mut self, node_id: NodeId) -> Self {
        self.node_id = Some(node_id);
        self
    }

    /// Enable diagnostics with a selected `implementation`.
    pub fn export_diagnostics(
        mut self,
        implementation: fidl_fuchsia_overnet_protocol::Implementation,
    ) -> Self {
        self.diagnostics = Some(implementation);
        self
    }
}

enum PendingTransfer {
    Complete(FoundTransfer),
    Waiting(Waker),
}

type PendingTransferMap = BTreeMap<TransferKey, PendingTransfer>;

#[derive(Debug)]
pub(crate) enum FoundTransfer {
    Fused(Handle),
    Remote(AsyncQuicStreamWriter, AsyncQuicStreamReader),
}

#[derive(Debug)]
pub(crate) enum OpenedTransfer {
    Fused,
    Remote(AsyncQuicStreamWriter, AsyncQuicStreamReader, Handle),
}

struct PeerMaps {
    clients: BTreeMap<NodeId, Arc<Peer>>,
    servers: BTreeMap<NodeId, Vec<Arc<Peer>>>,
    connections: HashMap<ConnectionId, Arc<Peer>>,
}

impl PeerMaps {
    async fn get_client(
        &mut self,
        peer_node_id: NodeId,
        router: &Arc<Router>,
    ) -> Result<Arc<Peer>, Error> {
        if peer_node_id == router.node_id {
            bail!("Trying to create loopback client peer");
        }
        if let Some(p) = self.clients.get(&peer_node_id) {
            if p.node_id() != peer_node_id {
                bail!(
                    "Existing client peer gets a packet from a new peer node id {:?} (vs {:?})",
                    peer_node_id,
                    p.node_id()
                );
            }
            Ok(p.clone())
        } else {
            self.new_client(peer_node_id, router).await
        }
    }

    async fn lookup(
        &mut self,
        conn_id: &[u8],
        ty: quiche::Type,
        peer_node_id: NodeId,
        router: &Arc<Router>,
    ) -> Result<Arc<Peer>, Error> {
        if peer_node_id == router.node_id {
            bail!("Trying to create loopback peer");
        }
        match (ty, conn_id.try_into().map(|conn_id| self.connections.get(&conn_id))) {
            (_, Ok(Some(p))) => {
                if p.node_id() != peer_node_id {
                    bail!(
                        "Existing looked-up peer gets a packet from a new peer node id {:?} (vs {:?})",
                        peer_node_id,
                        p.node_id()
                    );
                }
                Ok(p.clone())
            }
            (quiche::Type::Initial, _) => self.new_server(peer_node_id, router).await,
            (_, _) => bail!("Packet received for unknown connection"),
        }
    }

    async fn new_client(
        &mut self,
        peer_node_id: NodeId,
        router: &Arc<Router>,
    ) -> Result<Arc<Peer>, Error> {
        let mut config =
            router.quiche_config().await.context("creating client configuration for quiche")?;
        let conn_id = ConnectionId::new();
        let conn = quiche::connect(None, &conn_id.to_array(), &mut config)?;
        let peer = Peer::new_client(
            peer_node_id,
            conn_id,
            conn,
            router.service_map.new_local_service_observer(),
            router,
        );
        self.clients.insert(peer_node_id, peer.clone());
        self.connections.insert(conn_id, peer.clone());
        Ok(peer)
    }

    async fn new_server(
        &mut self,
        peer_node_id: NodeId,
        router: &Arc<Router>,
    ) -> Result<Arc<Peer>, Error> {
        let mut config =
            router.quiche_config().await.context("creating client configuration for quiche")?;
        let conn_id = ConnectionId::new();
        let conn = quiche::accept(&conn_id.to_array(), None, &mut config)?;
        let peer = Peer::new_server(peer_node_id, conn_id, conn, router);
        self.servers.entry(peer_node_id).or_insert(Vec::new()).push(peer.clone());
        self.connections.insert(conn_id, peer.clone());
        Ok(peer)
    }
}

/// Wrapper to get the right list_peers behavior.
pub struct ListPeersContext(Mutex<Option<Observer<Vec<ListablePeer>>>>);

static LIST_PEERS_CALL: AtomicU64 = AtomicU64::new(0);

impl ListPeersContext {
    /// Implementation of ListPeers fidl method.
    pub async fn list_peers(&self) -> Result<Vec<fidl_fuchsia_overnet::Peer>, Error> {
        let call_id = LIST_PEERS_CALL.fetch_add(1, Ordering::SeqCst);
        log::trace!("LIST_PEERS_CALL[{}] get observer", call_id);
        let mut obs = self
            .0
            .lock()
            .await
            .take()
            .ok_or_else(|| anyhow::format_err!("Already listing peers"))?;
        log::trace!("LIST_PEERS_CALL[{}] wait for value", call_id);
        let r = obs.next().await;
        log::trace!("LIST_PEERS_CALL[{}] replace observer", call_id);
        *self.0.lock().await = Some(obs);
        log::trace!("LIST_PEERS_CALL[{}] return", call_id);
        Ok(r.unwrap_or_else(Vec::new).into_iter().map(|p| p.into()).collect())
    }
}

/// Router maintains global state for one node_id.
/// `LinkData` is a token identifying a link for layers above Router.
/// `Time` is a representation of time for the Router, to assist injecting different platforms
/// schemes.
pub struct Router {
    /// Our node id
    node_id: NodeId,
    /// Factory for local link id's (the next id to be assigned).
    next_node_link_id: AtomicU64,
    security_context: Box<dyn SecurityContext>,
    /// All peers.
    peers: Mutex<PeerMaps>,
    links: Mutex<HashMap<NodeLinkId, Weak<LinkRouting>>>,
    link_state_publisher: LinkStatePublisher,
    service_map: ServiceMap,
    proxied_streams: Mutex<HashMap<HandleKey, ProxiedHandle>>,
    pending_transfers: Mutex<PendingTransferMap>,
    routes: Arc<Routes>,
    current_forwarding_table: Mutex<ForwardingTable>,
    task: Mutex<Option<Task<()>>>,
}

struct ProxiedHandle {
    remove_sender: futures::channel::oneshot::Sender<RemoveFromProxyTable>,
    original_paired: HandleKey,
    proxy_task: Task<()>,
}

/// Generate a new random node id
pub fn generate_node_id() -> NodeId {
    rand::thread_rng().gen::<u64>().into()
}

fn sorted<T: std::cmp::Ord>(mut v: Vec<T>) -> Vec<T> {
    v.sort();
    v
}

impl std::fmt::Debug for Router {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Router({:?})", self.node_id)
    }
}

impl Router {
    /// New with some set of options
    pub fn new(
        options: RouterOptions,
        security_context: Box<dyn SecurityContext>,
    ) -> Result<Arc<Self>, Error> {
        // Verify that we can read all files in the security_context - this makes debugging easier
        // later on (since errors from quiche are quite non-descript).
        let verify_file = |file: &str| {
            std::fs::File::open(file).context(format_err!("Opening {}", file)).map(drop)
        };
        verify_file(security_context.node_cert())?;
        verify_file(security_context.node_private_key())?;
        verify_file(security_context.root_cert())?;

        let node_id = options.node_id.unwrap_or_else(generate_node_id);
        let service_map = ServiceMap::new(node_id);
        let (link_state_publisher, link_state_receiver) = futures::channel::mpsc::channel(1);
        let routes = Arc::new(Routes::new());
        let router = Arc::new(Router {
            node_id,
            next_node_link_id: 1.into(),
            security_context,
            link_state_publisher,
            service_map,
            links: Mutex::new(HashMap::new()),
            peers: Mutex::new(PeerMaps {
                clients: BTreeMap::new(),
                servers: BTreeMap::new(),
                connections: HashMap::new(),
            }),
            proxied_streams: Mutex::new(HashMap::new()),
            pending_transfers: Mutex::new(PendingTransferMap::new()),
            routes: routes.clone(),
            task: Mutex::new(None),
            current_forwarding_table: Mutex::new(ForwardingTable::empty()),
        });

        let diagnostics = options.diagnostics;
        let link_state_observable = Observable::new(BTreeMap::new());
        let weak_router = Arc::downgrade(&router);
        *router.task.lock().now_or_never().unwrap() = Some(Task::spawn(log_errors(
            async move {
                let router = &weak_router;
                futures::future::try_join4(
                    summon_clients(router.clone(), routes.new_forwarding_table_observer()),
                    routes.run_planner(node_id, link_state_observable.new_observer()),
                    run_link_status_updater(node_id, link_state_observable, link_state_receiver),
                    async move {
                        if let Some(implementation) = diagnostics {
                            run_diagostic_service_request_handler(router, implementation).await?;
                        }
                        Ok(())
                    },
                )
                .await
                .map(drop)
            },
            format!("router {:?} support loop failed", node_id),
        )));

        Ok(router)
    }

    /// Create a new link to some node, returning a `LinkId` describing it.
    pub async fn new_link(
        self: &Arc<Self>,
        peer_node_id: NodeId,
        config: crate::link::ConfigProducer,
    ) -> Result<(LinkSender, LinkReceiver), Error> {
        let node_link_id = self.next_node_link_id.fetch_add(1, Ordering::Relaxed).into();
        let (sender, receiver, routing, observer) =
            new_link(peer_node_id, node_link_id, &self, config);
        log::trace!("[{:?} link {:?}] new link to {:?}", self.node_id, node_link_id, peer_node_id);
        self.links.lock().await.insert(routing.id(), Arc::downgrade(&routing));
        log::trace!("[{:?} link {:?}] publish link", self.node_id, node_link_id);
        self.link_state_publisher.clone().send((node_link_id, peer_node_id, observer)).await?;
        log::trace!("[{:?} link {:?}] return link", self.node_id, node_link_id);
        Ok((sender, receiver))
    }

    pub(crate) async fn get_link(&self, node_link_id: NodeLinkId) -> Option<Arc<LinkRouting>> {
        self.links.lock().await.get(&node_link_id).and_then(Weak::upgrade)
    }

    /// Accessor for the node id of this router.
    pub fn node_id(&self) -> NodeId {
        self.node_id
    }

    pub(crate) fn service_map(&self) -> &ServiceMap {
        &self.service_map
    }

    pub(crate) fn new_forwarding_table_observer(&self) -> Observer<ForwardingTable> {
        self.routes.new_forwarding_table_observer()
    }

    pub(crate) async fn update_routes(
        &self,
        via: NodeId,
        routes: impl Iterator<Item = (NodeId, RouteMetrics)>,
    ) {
        self.routes.update(via, routes).await
    }

    /// Create a new stream to advertised service `service` on remote node id `node`.
    pub async fn connect_to_service(
        self: &Arc<Self>,
        node_id: NodeId,
        service_name: &str,
        chan: Channel,
    ) -> Result<(), Error> {
        let is_local = node_id == self.node_id;
        log::trace!(
            "Request connect_to_service '{}' on {:?}{}",
            service_name,
            node_id,
            if is_local { " [local]" } else { " [remote]" }
        );
        if is_local {
            self.service_map()
                .connect(
                    service_name,
                    chan,
                    ConnectionInfo { peer: Some(node_id.into()), ..ConnectionInfo::empty() },
                )
                .await
        } else {
            self.client_peer(node_id)
                .await
                .with_context(|| {
                    format_err!(
                        "Fetching client peer for new stream to {:?} for service {:?}",
                        node_id,
                        service_name,
                    )
                })?
                .new_stream(service_name, chan, self)
                .await
        }
    }

    /// Implementation of RegisterService fidl method.
    pub async fn register_service(
        &self,
        service_name: String,
        provider: ClientEnd<ServiceProviderMarker>,
    ) -> Result<(), Error> {
        self.service_map().register_service(service_name, Box::new(provider.into_proxy()?)).await;
        Ok(())
    }

    /// Create a new list_peers context
    pub fn new_list_peers_context(&self) -> ListPeersContext {
        ListPeersContext(Mutex::new(Some(self.service_map.new_list_peers_observer())))
    }

    /// Implementation of AttachToSocket fidl method.
    pub async fn run_socket_link(
        self: &Arc<Self>,
        socket: fidl::Socket,
        options: fidl_fuchsia_overnet_protocol::SocketLinkOptions,
    ) -> Result<(), Error> {
        run_socket_link(self.clone(), socket, options).await
    }

    /// Diagnostic information for links
    pub(crate) async fn link_diagnostics(&self) -> Vec<LinkDiagnosticInfo> {
        futures::stream::iter(self.links.lock().await.iter())
            .filter_map(|(_, link)| async move {
                if let Some(link) = Weak::upgrade(link) {
                    if !link.is_closed().await {
                        return Some(link.diagnostic_info().await);
                    }
                }
                None
            })
            .collect()
            .await
    }

    /// Diagnostic information for peer connections
    pub(crate) async fn peer_diagnostics(&self) -> Vec<PeerConnectionDiagnosticInfo> {
        futures::stream::iter(self.peers.lock().await.connections.iter())
            .then(|(_, peer)| peer.diagnostics(self.node_id))
            .collect()
            .await
    }

    async fn quiche_config(&self) -> Result<quiche::Config, Error> {
        let mut config =
            quiche_config_from_security_context(&*self.security_context).await.with_context(
                || format_err!("applying security context: {:?}", self.security_context),
            )?;
        // TODO(ctiller): don't hardcode these
        config.set_application_protos(b"\x0bovernet/0.1")?;
        config.set_initial_max_data(10_000_000);
        config.set_initial_max_stream_data_bidi_local(1_000_000);
        config.set_initial_max_stream_data_bidi_remote(1_000_000);
        config.set_initial_max_stream_data_uni(1_000_000);
        config.set_initial_max_streams_bidi(100);
        config.set_initial_max_streams_uni(100);
        config.verify_peer(false);
        Ok(config)
    }

    pub(crate) async fn remove_peer(
        self: &Arc<Self>,
        conn_id: ConnectionId,
        due_to_routing_error: bool,
    ) {
        log::trace!("[{:?}] Request remove peer {:?}", self.node_id, conn_id);
        let mut removed_client_peer_node_id = None;
        {
            let mut peers = self.peers.lock().await;
            if let Some(peer) = peers.connections.remove(&conn_id) {
                match peer.endpoint() {
                    Endpoint::Client => {
                        if let btree_map::Entry::Occupied(o) = peers.clients.entry(peer.node_id()) {
                            if Arc::ptr_eq(o.get(), &peer) {
                                removed_client_peer_node_id = Some(peer.node_id());
                                o.remove_entry();
                            }
                        }
                    }
                    Endpoint::Server => {
                        peers
                            .servers
                            .get_mut(&peer.node_id())
                            .map(|v| v.retain(|other| !Arc::ptr_eq(&peer, other)));
                    }
                }
                peer.shutdown().await;
            }
        }
        if let Some(removed_client_peer_node_id) = removed_client_peer_node_id {
            log::trace!(
                "[{:?}] Removed client peer {:?} to {:?}",
                self.node_id,
                conn_id,
                removed_client_peer_node_id
            );
            let revive = !due_to_routing_error
                && self
                    .current_forwarding_table
                    .lock()
                    .await
                    .route_for(removed_client_peer_node_id)
                    .is_some();
            if revive {
                log::trace!(
                    "[{:?}] Revive client peer to {:?}",
                    self.node_id,
                    removed_client_peer_node_id
                );
                if let Err(e) =
                    self.peers.lock().await.get_client(removed_client_peer_node_id, self).await
                {
                    log::trace!(
                        "[{:?}] Failed revibing client peer to {:?}: {:?}",
                        self.node_id,
                        removed_client_peer_node_id,
                        e
                    );
                } else {
                    log::trace!(
                        "[{:?}] Revived client peer to {:?}",
                        self.node_id,
                        removed_client_peer_node_id
                    );
                }
            } else {
                log::trace!(
                    "[{:?}] Do not revive client peer to {:?}",
                    self.node_id,
                    removed_client_peer_node_id
                );
            }
        }
    }

    async fn client_peer(self: &Arc<Self>, peer_node_id: NodeId) -> Result<Arc<Peer>, Error> {
        self.peers.lock().await.get_client(peer_node_id, self).await
    }

    pub(crate) async fn lookup_peer(
        self: &Arc<Self>,
        conn_id: &[u8],
        ty: quiche::Type,
        peer_node_id: NodeId,
    ) -> Result<Arc<Peer>, Error> {
        self.peers.lock().await.lookup(conn_id, ty, peer_node_id, self).await
    }

    fn add_proxied(
        self: &Arc<Self>,
        proxied_streams: &mut HashMap<HandleKey, ProxiedHandle>,
        this_handle_key: HandleKey,
        pair_handle_key: HandleKey,
        remove_sender: futures::channel::oneshot::Sender<RemoveFromProxyTable>,
        f: impl 'static + Send + Future<Output = Result<(), Error>>,
    ) {
        let router = Arc::downgrade(&self);
        let proxy_task = Task::spawn(async move {
            if let Err(e) = f.await {
                log::trace!("[{:?}:{:?}] Proxy failed: {:?}", this_handle_key, pair_handle_key, e);
            } else {
                log::trace!(
                    "[{:?}:{:?}] Proxy completed successfully",
                    this_handle_key,
                    pair_handle_key
                );
            }
            if let Some(router) = Weak::upgrade(&router) {
                router.remove_proxied(this_handle_key, pair_handle_key).await;
            }
        });
        assert!(proxied_streams
            .insert(
                this_handle_key,
                ProxiedHandle { remove_sender, original_paired: pair_handle_key, proxy_task },
            )
            .is_none());
    }

    // Remove a proxied handle from our table.
    // Called by proxy::Proxy::drop.
    async fn remove_proxied(
        self: &Arc<Self>,
        this_handle_key: HandleKey,
        pair_handle_key: HandleKey,
    ) {
        let mut proxied_streams = self.proxied_streams.lock().await;
        log::trace!(
            "{:?} REMOVE_PROXIED: {:?}:{:?} all={:?}",
            self.node_id,
            this_handle_key,
            pair_handle_key,
            sorted(proxied_streams.keys().map(|x| *x).collect::<Vec<_>>())
        );
        if let Some(removed) = proxied_streams.remove(&this_handle_key) {
            assert_eq!(removed.original_paired, pair_handle_key);
            let _ = removed.remove_sender.send(RemoveFromProxyTable::Dropped);
            removed.proxy_task.detach();
        }
    }

    // Prepare a handle to be sent to another machine.
    // Returns a ZirconHandle describing the established proxy.
    pub(crate) async fn send_proxied(
        self: Arc<Self>,
        handle: Handle,
        conn: AsyncConnection,
        stats: Arc<MessageStats>,
    ) -> Result<ZirconHandle, Error> {
        let raw_handle = handle.raw_handle(); // for debugging
        let info = handle_info(handle.as_handle_ref())
            .with_context(|| format!("Getting handle information for {}", raw_handle))?;
        let mut proxied_streams = self.proxied_streams.lock().await;
        log::trace!(
            "{:?} SEND_PROXIED: {:?} {:?} all={:?}",
            self.node_id,
            handle,
            info,
            sorted(proxied_streams.keys().map(|x| *x).collect::<Vec<_>>())
        );
        if let Some(pair) = proxied_streams.remove(&info.pair_handle_key) {
            // This handle is the other end of an already proxied object...
            // Here we need to inform the existing proxy loop that a transfer is going to be
            // initiated, and to where.
            drop(proxied_streams);
            assert_eq!(info.this_handle_key, pair.original_paired);
            log::trace!("Send paired proxied {:?} orig_pair={:?}", handle, pair.original_paired);
            // We allocate a drain stream to flush any messages we've buffered locally to the new
            // endpoint.
            let drain_stream = conn.alloc_uni().into();
            let (stream_ref_sender, stream_ref_receiver) = StreamRefSender::new();
            pair.remove_sender
                .send(RemoveFromProxyTable::InitiateTransfer {
                    paired_handle: handle,
                    drain_stream,
                    stream_ref_sender,
                })
                .map_err(|_| format_err!("Failed to initiate transfer"))?;
            let stream_ref = stream_ref_receiver
                .await
                .with_context(|| format!("waiting for stream_ref for {:?}", raw_handle))?;
            pair.proxy_task.detach();
            match info.handle_type {
                HandleType::Channel(rights) => {
                    Ok(ZirconHandle::Channel(ChannelHandle { stream_ref, rights }))
                }
                HandleType::Socket(socket_type, rights) => {
                    Ok(ZirconHandle::Socket(SocketHandle { stream_ref, socket_type, rights }))
                }
            }
        } else {
            // This handle (and its pair) is previously unseen... establish a proxy stream for it
            log::trace!("Send proxied {:?}", handle);
            let (tx, rx) = futures::channel::oneshot::channel();
            let rx = ProxyTransferInitiationReceiver::new(rx.map_err(move |_| {
                format_err!(
                    "cancelled transfer via send_proxied {:?}\n{}",
                    info,
                    111 //std::backtrace::Backtrace::force_capture()
                )
            }));
            let (stream_writer, stream_reader) = conn.alloc_bidi();
            let stream_ref = StreamRef::Creating(StreamId { id: stream_writer.id() });
            Ok(match info.handle_type {
                HandleType::Channel(rights) => {
                    self.add_proxied(
                        &mut *proxied_streams,
                        info.this_handle_key,
                        info.pair_handle_key,
                        tx,
                        crate::proxy::spawn::send(
                            Channel::from_handle(handle).into_proxied()?,
                            rx,
                            stream_writer.into(),
                            stream_reader.into(),
                            stats,
                            Arc::downgrade(&self),
                        ),
                    );
                    ZirconHandle::Channel(ChannelHandle { stream_ref, rights })
                }
                HandleType::Socket(socket_type, rights) => {
                    self.add_proxied(
                        &mut *proxied_streams,
                        info.this_handle_key,
                        info.pair_handle_key,
                        tx,
                        crate::proxy::spawn::send(
                            Socket::from_handle(handle).into_proxied()?,
                            rx,
                            stream_writer.into(),
                            stream_reader.into(),
                            stats,
                            Arc::downgrade(&self),
                        ),
                    );
                    ZirconHandle::Socket(SocketHandle { stream_ref, socket_type, rights })
                }
            })
        }
    }

    // Take a received handle description and construct a fidl::Handle that represents it
    // whilst establishing proxies as required
    pub(crate) async fn recv_proxied(
        self: Arc<Self>,
        handle: ZirconHandle,
        conn: AsyncConnection,
        stats: Arc<MessageStats>,
    ) -> Result<Handle, Error> {
        let (tx, rx) = futures::channel::oneshot::channel();
        let debug_id = generate_node_id().0;
        let rx = ProxyTransferInitiationReceiver::new(rx.map_err(move |_| {
            format_err!("cancelled transfer via recv_proxied; debug_id={}", debug_id)
        }));
        Ok(match handle {
            ZirconHandle::Channel(ChannelHandle { stream_ref, rights }) => {
                let (h, p) = crate::proxy::spawn::recv(
                    move || Channel::create().map_err(Into::into),
                    rights,
                    rx,
                    stream_ref,
                    &conn,
                    stats,
                    Arc::downgrade(&self),
                )
                .await?;
                if let Some(p) = p {
                    let info = handle_info(h.as_handle_ref())?;
                    self.add_proxied(
                        &mut *self.proxied_streams.lock().await,
                        info.pair_handle_key,
                        info.this_handle_key,
                        tx,
                        p,
                    );
                }
                h
            }
            ZirconHandle::Socket(SocketHandle { stream_ref, socket_type, rights }) => {
                let opts = match socket_type {
                    SocketType::Stream => SocketOpts::STREAM,
                    SocketType::Datagram => SocketOpts::DATAGRAM,
                };
                let (h, p) = crate::proxy::spawn::recv(
                    move || Socket::create(opts).map_err(Into::into),
                    rights,
                    rx,
                    stream_ref,
                    &conn,
                    stats,
                    Arc::downgrade(&self),
                )
                .await?;
                if let Some(p) = p {
                    let info = handle_info(h.as_handle_ref())?;
                    self.add_proxied(
                        &mut *self.proxied_streams.lock().await,
                        info.pair_handle_key,
                        info.this_handle_key,
                        tx,
                        p,
                    );
                }
                h
            }
        })
    }

    // Note the endpoint of a transfer that we know about (may complete a transfer operation)
    pub(crate) async fn post_transfer(
        &self,
        transfer_key: TransferKey,
        other_end: FoundTransfer,
    ) -> Result<(), Error> {
        let mut pending_transfers = self.pending_transfers.lock().await;
        match pending_transfers.insert(transfer_key, PendingTransfer::Complete(other_end)) {
            Some(PendingTransfer::Complete(_)) => bail!("Duplicate transfer received"),
            Some(PendingTransfer::Waiting(w)) => w.wake(),
            None => (),
        }
        Ok(())
    }

    fn poll_find_transfer(
        &self,
        ctx: &mut Context<'_>,
        transfer_key: TransferKey,
        lock: &mut PollMutex<'_, PendingTransferMap>,
    ) -> Poll<Result<FoundTransfer, Error>> {
        let mut pending_transfers = ready!(lock.poll(ctx));
        if let Some(PendingTransfer::Complete(other_end)) = pending_transfers.remove(&transfer_key)
        {
            Poll::Ready(Ok(other_end))
        } else {
            pending_transfers.insert(transfer_key, PendingTransfer::Waiting(ctx.waker().clone()));
            Poll::Pending
        }
    }

    // Lookup a transfer that we're expected to eventually know about
    pub(crate) async fn find_transfer(
        &self,
        transfer_key: TransferKey,
    ) -> Result<FoundTransfer, Error> {
        let mut lock = PollMutex::new(&self.pending_transfers);
        poll_fn(|ctx| self.poll_find_transfer(ctx, transfer_key, &mut lock)).await
    }

    // Begin a transfer operation (opposite of find_transfer), publishing an endpoint on the remote
    // nodes transfer table.
    pub(crate) async fn open_transfer(
        self: &Arc<Router>,
        target: NodeId,
        transfer_key: TransferKey,
        handle: Handle,
    ) -> Result<OpenedTransfer, Error> {
        if target == self.node_id {
            // The target is local: we just file away the handle.
            // Later, find_transfer will find this and we'll collapse away Overnet's involvement and
            // reunite the channel ends.
            let info = handle_info(handle.as_handle_ref())?;
            let mut proxied_streams = self.proxied_streams.lock().await;
            log::trace!(
                "{:?} OPEN_TRANSFER_REMOVE_PROXIED: key={:?} {:?} all={:?}",
                self.node_id,
                transfer_key,
                info,
                sorted(proxied_streams.keys().map(|x| *x).collect::<Vec<_>>())
            );
            if let Some(removed) = proxied_streams.remove(&info.this_handle_key) {
                assert_eq!(removed.original_paired, info.pair_handle_key);
                assert!(removed.remove_sender.send(RemoveFromProxyTable::Dropped).is_ok());
                removed.proxy_task.detach();
            }
            if let Some(removed) = proxied_streams.remove(&info.pair_handle_key) {
                assert_eq!(removed.original_paired, info.this_handle_key);
                assert!(removed.remove_sender.send(RemoveFromProxyTable::Dropped).is_ok());
                removed.proxy_task.detach();
            }
            self.post_transfer(transfer_key, FoundTransfer::Fused(handle)).await?;
            Ok(OpenedTransfer::Fused)
        } else {
            let (writer, reader) = loop {
                if let Some(x) =
                    self.client_peer(target).await?.send_open_transfer(transfer_key).await
                {
                    break x;
                }
                log::warn!(
                    "{:?} failed sending open transfer to {:?}; retrying",
                    self.node_id,
                    target
                );
            };
            Ok(OpenedTransfer::Remote(writer, reader, handle))
        }
    }

    pub(crate) fn security_context(&self) -> &dyn SecurityContext {
        &*self.security_context
    }
}

async fn summon_clients(
    router: Weak<Router>,
    mut forwarding_table: Observer<ForwardingTable>,
) -> Result<(), Error> {
    let get_router = move || Weak::upgrade(&router).ok_or_else(|| format_err!("router gone"));
    while let Some(forwarding_table) = forwarding_table.next().await {
        let router = get_router()?;
        *router.current_forwarding_table.lock().await = forwarding_table.clone();
        let mut peers = router.peers.lock().await;
        for (destination, _) in forwarding_table.iter() {
            let _ = peers.get_client(destination, &router).await?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {

    use super::*;
    use crate::test_util::*;

    #[fuchsia_async::run(1, test)]
    async fn no_op(run: usize) {
        crate::test_util::init();

        let mut node_id_gen = NodeIdGenerator::new("router::no_op", run);
        node_id_gen.new_router().unwrap();
        let id = node_id_gen.next().unwrap();
        assert_eq!(
            Router::new(RouterOptions::new().set_node_id(id), Box::new(test_security_context()))
                .unwrap()
                .node_id,
            id
        );
    }

    async fn forward(sender: LinkSender, mut receiver: LinkReceiver) -> Result<(), Error> {
        while let Some(packet) = sender.next_send().await {
            let mut bytes = packet.bytes().to_vec();
            drop(packet);
            log::trace!("got frame {} bytes", bytes.len());
            if let Err(e) = receiver.received_packet(&mut bytes).await {
                log::warn!("error receiving packet: {:?}", e);
            }
        }
        Ok(())
    }

    async fn register_test_service(
        serving_router: Arc<Router>,
        client_router: Arc<Router>,
        service: &'static str,
    ) -> futures::channel::oneshot::Receiver<(NodeId, Channel)> {
        use parking_lot::Mutex;
        let (send, recv) = futures::channel::oneshot::channel();
        struct TestService(
            Mutex<Option<futures::channel::oneshot::Sender<(NodeId, Channel)>>>,
            &'static str,
        );
        impl fidl_fuchsia_overnet::ServiceProviderProxyInterface for TestService {
            fn connect_to_service(
                &self,
                chan: fidl::Channel,
                connection_info: fidl_fuchsia_overnet::ConnectionInfo,
            ) -> std::result::Result<(), fidl::Error> {
                println!("{} got request", self.1);
                self.0
                    .lock()
                    .take()
                    .unwrap()
                    .send((connection_info.peer.unwrap().id.into(), chan))
                    .unwrap();
                println!("{} forwarded channel", self.1);
                Ok(())
            }
        }
        serving_router
            .service_map()
            .register_service(
                service.to_string(),
                Box::new(TestService(Mutex::new(Some(send)), service)),
            )
            .await;
        let serving_node_id = serving_router.node_id();
        println!("{} wait for service to appear @ client", service);
        let lpc = client_router.new_list_peers_context();
        loop {
            let peers = lpc.list_peers().await.unwrap();
            println!("{} got peers {:?}", service, peers);
            if peers
                .iter()
                .find(move |peer| {
                    serving_node_id == peer.id.into()
                        && peer
                            .description
                            .services
                            .as_ref()
                            .unwrap()
                            .iter()
                            .find(move |&advertised_service| advertised_service == service)
                            .is_some()
                })
                .is_some()
            {
                break;
            }
        }
        recv
    }

    async fn run_two_node<
        F: 'static + Clone + Sync + Send + Fn(Arc<Router>, Arc<Router>) -> Fut,
        Fut: 'static + Send + Future<Output = Result<(), Error>>,
    >(
        name: &'static str,
        run: usize,
        f: F,
    ) -> Result<(), Error> {
        let mut node_id_gen = NodeIdGenerator::new(name, run);
        let router1 = node_id_gen.new_router()?;
        let router2 = node_id_gen.new_router()?;
        let (link1_sender, link1_receiver) =
            router1.new_link(router2.node_id, Box::new(|| None)).await?;
        let (link2_sender, link2_receiver) =
            router2.new_link(router1.node_id, Box::new(|| None)).await?;
        let _fwd = Task::spawn(async move {
            if let Err(e) = futures::future::try_join(
                forward(link1_sender, link2_receiver),
                forward(link2_sender, link1_receiver),
            )
            .await
            {
                log::trace!("forwarding failed: {:?}", e)
            }
        });
        f(router1, router2).await
    }

    #[fuchsia_async::run(1, test)]
    async fn no_op_env(run: usize) -> Result<(), Error> {
        crate::test_util::init();

        run_two_node("router::no_op_env", run, |_router1, _router2| async { Ok(()) }).await
    }

    #[fuchsia_async::run(1, test)]
    async fn create_stream(run: usize) -> Result<(), Error> {
        crate::test_util::init();

        run_two_node("create_stream", run, |router1, router2| async move {
            let (_, p) = fidl::Channel::create()?;
            println!("create_stream: register service");
            let s = register_test_service(router2.clone(), router1.clone(), "create_stream").await;
            println!("create_stream: connect to service");
            router1.connect_to_service(router2.node_id, "create_stream", p).await?;
            println!("create_stream: wait for connection");
            let (node_id, _) = s.await?;
            assert_eq!(node_id, router1.node_id);
            Ok(())
        })
        .await
    }

    #[fuchsia_async::run(1, test)]
    async fn send_datagram_immediately(run: usize) -> Result<(), Error> {
        crate::test_util::init();

        run_two_node("send_datagram_immediately", run, |router1, router2| async move {
            let (c, p) = fidl::Channel::create()?;
            println!("send_datagram_immediately: register service");
            let s = register_test_service(
                router2.clone(),
                router1.clone(),
                "send_datagram_immediately",
            )
            .await;
            println!("send_datagram_immediately: connect to service");
            router1.connect_to_service(router2.node_id, "send_datagram_immediately", p).await?;
            println!("send_datagram_immediately: wait for connection");
            let (node_id, s) = s.await?;
            assert_eq!(node_id, router1.node_id);
            let c = fidl::AsyncChannel::from_channel(c)?;
            let s = fidl::AsyncChannel::from_channel(s)?;
            c.write(&[1, 2, 3, 4, 5], &mut Vec::new())?;
            let mut buf = fidl::MessageBufEtc::new();
            println!("send_datagram_immediately: wait for datagram");
            s.recv_etc_msg(&mut buf).await?;
            assert_eq!(buf.n_handle_infos(), 0);
            assert_eq!(buf.bytes(), &[1, 2, 3, 4, 5]);
            Ok(())
        })
        .await
    }

    #[fuchsia_async::run(1, test)]
    async fn ping_pong(run: usize) -> Result<(), Error> {
        run_two_node("ping_pong", run, |router1, router2| async move {
            crate::test_util::init();

            let (c, p) = fidl::Channel::create()?;
            println!("ping_pong: register service");
            let s = register_test_service(router2.clone(), router1.clone(), "ping_pong").await;
            println!("ping_pong: connect to service");
            router1.connect_to_service(router2.node_id, "ping_pong", p).await?;
            println!("ping_pong: wait for connection");
            let (node_id, s) = s.await?;
            assert_eq!(node_id, router1.node_id);
            let c = fidl::AsyncChannel::from_channel(c)?;
            let s = fidl::AsyncChannel::from_channel(s)?;
            println!("ping_pong: send ping");
            c.write(&[1, 2, 3, 4, 5], &mut Vec::new())?;
            println!("ping_pong: receive ping");
            let mut buf = fidl::MessageBufEtc::new();
            s.recv_etc_msg(&mut buf).await?;
            assert_eq!(buf.n_handle_infos(), 0);
            assert_eq!(buf.bytes(), &[1, 2, 3, 4, 5]);
            println!("ping_pong: send pong");
            s.write(&[9, 8, 7, 6, 5, 4, 3, 2, 1], &mut Vec::new())?;
            println!("ping_pong: receive pong");
            let mut buf = fidl::MessageBufEtc::new();
            c.recv_etc_msg(&mut buf).await?;
            assert_eq!(buf.n_handle_infos(), 0);
            assert_eq!(buf.bytes(), &[9, 8, 7, 6, 5, 4, 3, 2, 1]);
            Ok(())
        })
        .await
    }

    fn ensure_pending(f: &mut (impl Send + Unpin + Future<Output = ()>)) {
        let mut ctx = Context::from_waker(futures::task::noop_waker_ref());
        // Poll a bunch of times to convince ourselves the future is pending forever...
        for _ in 0..1000 {
            assert!(f.poll_unpin(&mut ctx).is_pending());
        }
    }

    #[fuchsia_async::run(1, test)]
    async fn concurrent_list_peer_calls_will_error(run: usize) -> Result<(), Error> {
        crate::test_util::init();

        let mut node_id_gen = NodeIdGenerator::new("concurrent_list_peer_calls_will_error", run);
        let n = node_id_gen.new_router().unwrap();
        let lp = n.new_list_peers_context();
        lp.list_peers().await.unwrap();
        let mut never_completes = async {
            lp.list_peers().await.unwrap();
        }
        .boxed();
        ensure_pending(&mut never_completes);
        lp.list_peers().await.expect_err("Concurrent list peers should fail");
        ensure_pending(&mut never_completes);
        Ok(())
    }

    #[fuchsia_async::run(1, test)]
    async fn initial_greeting_packet(run: usize) -> Result<(), Error> {
        crate::test_util::init();

        use crate::coding::decode_fidl;
        use crate::stream_framer::{new_deframer, FrameType, LosslessBinary};
        use fidl_fuchsia_overnet_protocol::StreamSocketGreeting;
        let mut node_id_gen = NodeIdGenerator::new("initial_greeting_packet", run);
        let n = node_id_gen.new_router()?;
        let node_id = n.node_id();
        let (c, s) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
        let mut c = fidl::AsyncSocket::from_socket(c)?;
        let (mut deframer_writer, mut deframer) = new_deframer(LosslessBinary);
        let _s = Task::spawn(async move {
            n.run_socket_link(
                s,
                fidl_fuchsia_overnet_protocol::SocketLinkOptions {
                    connection_label: Some("test".to_string()),
                    bytes_per_second: None,
                    ..fidl_fuchsia_overnet_protocol::SocketLinkOptions::empty()
                },
            )
            .await
            .unwrap();
        });
        let _d = Task::spawn(async move {
            let mut buf = [0u8; 1024];
            loop {
                let n = c.read(&mut buf).await.unwrap();
                if n == 0 {
                    log::info!("initial_greeting_packet: socket closed");
                    return;
                }
                deframer_writer.write(&buf[..n]).await.unwrap();
            }
        });
        let (frame_type, mut greeting_bytes) = deframer.read().await?;
        assert_eq!(frame_type, Some(FrameType::OvernetHello));
        let greeting = decode_fidl::<StreamSocketGreeting>(greeting_bytes.as_mut())?;
        assert_eq!(greeting.magic_string, Some("OVERNET SOCKET LINK".to_string()));
        assert_eq!(greeting.node_id, Some(node_id.into()));
        assert_eq!(greeting.connection_label, Some("test".to_string()));
        Ok(())
    }

    #[fuchsia_async::run(1, test)]
    async fn attach_with_zero_bytes_per_second(run: usize) -> Result<(), Error> {
        crate::test_util::init();
        let mut node_id_gen = NodeIdGenerator::new("attach_with_zero_bytes_per_second", run);
        let n = node_id_gen.new_router()?;
        let (c, s) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
        n.run_socket_link(
            s,
            fidl_fuchsia_overnet_protocol::SocketLinkOptions {
                connection_label: Some("test".to_string()),
                bytes_per_second: Some(0),
                ..fidl_fuchsia_overnet_protocol::SocketLinkOptions::empty()
            },
        )
        .await
        .expect_err("bytes_per_second == 0 should fail");
        drop(c);
        Ok(())
    }
}
