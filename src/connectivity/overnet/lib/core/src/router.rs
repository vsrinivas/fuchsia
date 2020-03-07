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
    diagnostics_service::spawn_diagostic_service_request_handler,
    framed_stream::MessageStats,
    future_help::{Observable, Observer},
    handle_info::{handle_info, HandleKey, HandleType},
    labels::{Endpoint, NodeId, NodeLinkId, TransferKey},
    link::{Link, LinkStatus},
    link_status_updater::{spawn_link_status_updater, LinkStatePublisher},
    peer::Peer,
    proxy::{ProxyTransferInitiationReceiver, RemoveFromProxyTable, StreamRefSender},
    proxyable_handle::IntoProxied,
    route_planner::{
        routing_update_channel, spawn_route_planner, RemoteRoutingUpdate, RemoteRoutingUpdateSender,
    },
    runtime::spawn,
    service_map::{ListablePeer, ServiceMap},
    socket_link::spawn_socket_link,
};
use anyhow::{bail, format_err, Context as _, Error};
use fidl::{endpoints::ClientEnd, AsHandleRef, Channel, Handle, HandleBased, Socket, SocketOpts};
use fidl_fuchsia_overnet::{ConnectionInfo, ServiceProviderMarker};
use fidl_fuchsia_overnet_protocol::{
    ChannelHandle, LinkDiagnosticInfo, PeerConnectionDiagnosticInfo, SocketHandle, SocketType,
    StreamId, StreamRef, ZirconHandle,
};
use futures::{future::poll_fn, lock::Mutex, prelude::*};
use rand::Rng;
use std::{
    cell::RefCell,
    collections::{BTreeMap, HashMap},
    path::Path,
    rc::{Rc, Weak},
    sync::atomic::{AtomicU64, Ordering},
    task::{Context, Poll, Waker},
    time::Duration,
};

/// Configuration object for creating a router.
pub struct RouterOptions {
    node_id: Option<NodeId>,
    pub(crate) quic_server_key_file: Option<Box<dyn AsRef<Path>>>,
    pub(crate) quic_server_cert_file: Option<Box<dyn AsRef<Path>>>,
    diagnostics: Option<fidl_fuchsia_overnet_protocol::Implementation>,
}

impl RouterOptions {
    /// Create with defaults.
    pub fn new() -> Self {
        RouterOptions {
            node_id: None,
            quic_server_key_file: None,
            quic_server_cert_file: None,
            diagnostics: None,
        }
    }

    /// Request a specific node id (if unset, one will be generated).
    pub fn set_node_id(mut self, node_id: NodeId) -> Self {
        self.node_id = Some(node_id);
        self
    }

    /// Set which file to load the server private key from.
    pub fn set_quic_server_key_file(mut self, key_file: Box<dyn AsRef<Path>>) -> Self {
        self.quic_server_key_file = Some(key_file);
        self
    }

    /// Set which file to load the server cert from.
    pub fn set_quic_server_cert_file(mut self, pem_file: Box<dyn AsRef<Path>>) -> Self {
        self.quic_server_cert_file = Some(pem_file);
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

/// Router maintains global state for one node_id.
/// `LinkData` is a token identifying a link for layers above Router.
/// `Time` is a representation of time for the Router, to assist injecting different platforms
/// schemes.
pub struct Router {
    /// Our node id
    node_id: NodeId,
    /// Factory for local link id's (the next id to be assigned).
    next_node_link_id: AtomicU64,
    /// The servers private key file for this node.
    server_key_file: Box<dyn AsRef<Path>>,
    /// The servers private cert file for this node.
    server_cert_file: Box<dyn AsRef<Path>>,
    /// All peers.
    peers: Mutex<BTreeMap<(NodeId, Endpoint), Rc<Peer>>>,
    links: Mutex<HashMap<NodeLinkId, Weak<Link>>>,
    link_state_publisher: LinkStatePublisher,
    link_state_observable: Rc<Observable<Vec<LinkStatus>>>,
    service_map: ServiceMap,
    routing_update_sender: RemoteRoutingUpdateSender,
    list_peers_observer: Mutex<Option<Observer<Vec<ListablePeer>>>>,
    proxied_streams: RefCell<HashMap<HandleKey, ProxiedHandle>>,
    pending_transfers: RefCell<BTreeMap<TransferKey, PendingTransfer>>,
}

struct ProxiedHandle {
    remove_sender: futures::channel::oneshot::Sender<RemoveFromProxyTable>,
    original_paired: HandleKey,
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
    pub fn new(options: RouterOptions) -> Result<Rc<Self>, Error> {
        let node_id = options.node_id.unwrap_or_else(generate_node_id);
        let server_cert_file = options
            .quic_server_cert_file
            .ok_or_else(|| anyhow::format_err!("No server cert file"))?;
        if !server_cert_file.as_ref().as_ref().exists() {
            anyhow::bail!(
                "Server cert file does not exist: {}",
                server_cert_file.as_ref().as_ref().display()
            );
        }
        let server_key_file = options
            .quic_server_key_file
            .ok_or_else(|| anyhow::format_err!("No server key file"))?;
        if !server_key_file.as_ref().as_ref().exists() {
            anyhow::bail!(
                "Server key file does not exist: {}",
                server_key_file.as_ref().as_ref().display()
            );
        }
        let (routing_update_sender, routing_update_receiver) = routing_update_channel();
        let service_map = ServiceMap::new(node_id);
        let list_peers_observer = Mutex::new(Some(service_map.new_list_peers_observer()));
        let (link_state_publisher, link_state_receiver) = futures::channel::mpsc::channel(1);
        let router = Rc::new(Router {
            node_id,
            next_node_link_id: 1.into(),
            server_cert_file,
            server_key_file,
            routing_update_sender,
            link_state_publisher,
            link_state_observable: Rc::new(Observable::new(Vec::new())),
            service_map,
            links: Mutex::new(HashMap::new()),
            peers: Mutex::new(BTreeMap::new()),
            list_peers_observer,
            proxied_streams: RefCell::new(HashMap::new()),
            pending_transfers: RefCell::new(BTreeMap::new()),
        });

        spawn_route_planner(
            router.clone(),
            routing_update_receiver,
            router.link_state_observable.new_observer(),
        );
        spawn_link_status_updater(router.link_state_observable.clone(), link_state_receiver);
        // Spawn a future to service diagnostic requests
        if let Some(implementation) = options.diagnostics {
            spawn_diagostic_service_request_handler(router.clone(), implementation);
        }
        Ok(router)
    }

    /// Create a new link to some node, returning a `LinkId` describing it.
    pub async fn new_link(self: &Rc<Self>, peer_node_id: NodeId) -> Result<Rc<Link>, Error> {
        let node_link_id = self.next_node_link_id.fetch_add(1, Ordering::Relaxed).into();
        let link = Link::new(peer_node_id, node_link_id, &self).await?;
        self.link_state_publisher
            .clone()
            .send(
                link.new_round_trip_time_observer()
                    .map(move |rtt| (node_link_id, peer_node_id, rtt))
                    .boxed_local(),
            )
            .await?;
        self.links.lock().await.insert(link.id(), Rc::downgrade(&link));
        Ok(link)
    }

    /// Accessor for the node id of this router.
    pub fn node_id(&self) -> NodeId {
        self.node_id
    }

    pub(crate) fn service_map(&self) -> &ServiceMap {
        &self.service_map
    }

    /// Create a new stream to advertised service `service` on remote node id `node`.
    pub async fn connect_to_service(
        self: &Rc<Self>,
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
                .connect(service_name, chan, ConnectionInfo { peer: Some(node_id.into()) })
                .await
        } else {
            self.client_peer(node_id, &Weak::new())
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

    /// Implementation of ListPeers fidl method.
    pub async fn list_peers(
        self: &Rc<Self>,
        sink: Box<dyn FnOnce(Vec<fidl_fuchsia_overnet::Peer>)>,
    ) -> Result<(), Error> {
        let mut obs = self
            .list_peers_observer
            .lock()
            .await
            .take()
            .ok_or_else(|| anyhow::format_err!("Already listing peers"))?;
        let router = self.clone();
        spawn(async move {
            if let Some(r) = obs.next().await {
                *router.list_peers_observer.lock().await = Some(obs);
                sink(r.into_iter().map(|p| p.into()).collect());
            } else {
                *router.list_peers_observer.lock().await = Some(obs);
            }
        });
        Ok(())
    }

    /// Implementation of AttachToSocket fidl method.
    pub fn attach_socket_link(
        self: &Rc<Self>,
        socket: fidl::Socket,
        options: fidl_fuchsia_overnet::SocketLinkOptions,
    ) -> Result<(), Error> {
        let duration_per_byte = if let Some(n) = options.bytes_per_second {
            Some(std::cmp::max(
                Duration::from_micros(10),
                Duration::from_secs(1)
                    .checked_div(n)
                    .ok_or_else(|| anyhow::format_err!("Division failed: 1 second / {}", n))?,
            ))
        } else {
            None
        };
        spawn_socket_link(
            self.clone(),
            self.node_id,
            options.connection_label,
            socket,
            duration_per_byte,
        );
        Ok(())
    }

    /// Diagnostic information for links
    pub(crate) async fn link_diagnostics(&self) -> Vec<LinkDiagnosticInfo> {
        self.links
            .lock()
            .await
            .iter()
            .filter_map(|(_, link)| Weak::upgrade(link).map(|link| link.diagnostic_info()))
            .collect()
    }

    /// Diagnostic information for peer connections
    pub(crate) async fn peer_diagnostics(&self) -> Vec<PeerConnectionDiagnosticInfo> {
        self.peers.lock().await.iter().map(|(_, peer)| peer.diagnostics(self.node_id)).collect()
    }

    fn server_config(&self) -> Result<quiche::Config, Error> {
        let mut config = quiche::Config::new(quiche::PROTOCOL_VERSION)
            .context("Creating quic configuration for server connection")?;
        let cert_file = self
            .server_cert_file
            .as_ref()
            .as_ref()
            .to_str()
            .ok_or_else(|| format_err!("Cannot convert path to string"))?;
        let key_file = self
            .server_key_file
            .as_ref()
            .as_ref()
            .to_str()
            .ok_or_else(|| format_err!("Cannot convert path to string"))?;
        config
            .load_cert_chain_from_pem_file(cert_file)
            .context(format!("Loading server certificate '{}'", cert_file))?;
        config
            .load_priv_key_from_pem_file(key_file)
            .context(format!("Loading server private key '{}'", key_file))?;
        // TODO(ctiller): don't hardcode these
        config
            .set_application_protos(b"\x0bovernet/0.1")
            .context("Setting application protocols")?;
        config.set_initial_max_data(10_000_000);
        config.set_initial_max_stream_data_bidi_local(1_000_000);
        config.set_initial_max_stream_data_bidi_remote(1_000_000);
        config.set_initial_max_stream_data_uni(1_000_000);
        config.set_initial_max_streams_bidi(100);
        config.set_initial_max_streams_uni(100);
        Ok(config)
    }

    fn client_config(&self) -> Result<quiche::Config, Error> {
        let mut config = quiche::Config::new(quiche::PROTOCOL_VERSION)?;
        // TODO(ctiller): don't hardcode these
        config.set_application_protos(b"\x0bovernet/0.1")?;
        config.set_initial_max_data(10_000_000);
        config.set_initial_max_stream_data_bidi_local(1_000_000);
        config.set_initial_max_stream_data_bidi_remote(1_000_000);
        config.set_initial_max_stream_data_uni(1_000_000);
        config.set_initial_max_streams_bidi(100);
        config.set_initial_max_streams_uni(100);
        Ok(config)
    }

    pub(crate) async fn send_routing_update(&self, routing_update: RemoteRoutingUpdate) {
        if let Err(e) = self.routing_update_sender.clone().send(routing_update).await {
            log::warn!("Routing update send failed: {:?}", e);
        }
    }

    /// Ensure that a client peer id exists for a given `node_id`
    /// If the client peer needs to be created, `link_id_hint` will be used as an interim hint
    /// as to the best route *to* this node, until a full routing update can be performed.
    pub(crate) async fn ensure_client_peer(
        self: &Rc<Self>,
        node_id: NodeId,
        link_hint: &Weak<Link>,
    ) -> Result<(), Error> {
        if node_id == self.node_id {
            return Ok(());
        }
        self.client_peer(node_id, link_hint).await.map(|_| ())
    }

    /// Map a node id to the client peer id. Since we can always create a client peer, this will
    /// attempt to initiate a connection if there is no current connection.
    /// If the client peer needs to be created, `link_id_hint` will be used as an interim hint
    /// as to the best route *to* this node, until a full routing update can be performed.
    pub(crate) async fn client_peer(
        self: &Rc<Self>,
        node_id: NodeId,
        link_hint: &Weak<Link>,
    ) -> Result<Rc<Peer>, Error> {
        if node_id == self.node_id {
            bail!("Trying to create loopback client peer");
        }
        let key = (node_id, Endpoint::Client);
        let mut peers = self.peers.lock().await;
        if let Some(p) = peers.get(&key) {
            return Ok(p.clone());
        }

        let mut config = self.client_config()?;
        let scid: Vec<u8> = rand::thread_rng()
            .sample_iter(&rand::distributions::Standard)
            .take(quiche::MAX_CONN_ID_LEN)
            .collect();
        let p = Peer::new_client(
            self.node_id,
            node_id,
            quiche::connect(None, &scid, &mut config).context("creating quic client connection")?,
            link_hint,
            self.link_state_observable.new_observer(),
            self.service_map.new_local_service_observer(),
        )
        .context("creating client peer")?;
        peers.insert(key, p.clone());
        Ok(p)
    }

    /// Retrieve an existing server peer id for some node id.
    pub(crate) async fn server_peer(
        self: &Rc<Self>,
        node_id: NodeId,
        is_initial_packet: bool,
        link_hint: &Weak<Link>,
    ) -> Result<Option<Rc<Peer>>, Error> {
        if node_id == self.node_id {
            bail!("Trying to create loopback server peer");
        }
        let mut peers = self.peers.lock().await;
        let key = (node_id, Endpoint::Server);
        if let Some(p) = peers.get(&key) {
            return Ok(Some(p.clone()));
        }

        if !is_initial_packet {
            return Ok(None);
        }

        let mut config = self.server_config()?;
        let scid: Vec<u8> = rand::thread_rng()
            .sample_iter(&rand::distributions::Standard)
            .take(quiche::MAX_CONN_ID_LEN)
            .collect();
        let p = Peer::new_server(
            node_id,
            quiche::accept(&scid, None, &mut config).context("Creating quic server connection")?,
            link_hint,
            &self,
        )
        .context("creating server peer")?;
        assert!(peers.insert(key, p.clone()).is_none());
        Ok(Some(p))
    }

    pub(crate) async fn update_routes(
        self: &Rc<Self>,
        routes: impl Iterator<Item = (NodeId, NodeLinkId)>,
    ) -> Result<(), Error> {
        let links = self.links.lock().await;
        let dummy = Weak::new();
        for (node_id, link_id) in routes {
            let link = links.get(&link_id).unwrap_or(&dummy);
            if let Some(peer) = self.server_peer(node_id, false, &Weak::new()).await? {
                peer.update_link(link.clone()).await;
            }
            self.client_peer(node_id, &link)
                .await
                .with_context(|| format_err!("Adjusting route to link {:?}", link_id))?
                .update_link(link.clone())
                .await;
        }
        Ok(())
    }

    // Prepare a handle to be sent to another machine.
    // Returns a ZirconHandle describing the established proxy.
    pub(crate) async fn send_proxied(
        self: Rc<Self>,
        handle: Handle,
        conn: AsyncConnection,
        stats: Rc<MessageStats>,
    ) -> Result<ZirconHandle, Error> {
        let info = handle_info(handle.as_handle_ref())?;
        let mut proxied_streams = self.proxied_streams.borrow_mut();
        log::trace!(
            "{:?} SEND_PROXIED: {:?} all={:?}",
            self.node_id,
            info,
            sorted(proxied_streams.keys().map(|x| *x).collect::<Vec<_>>())
        );
        if let Some(pair) = proxied_streams.remove(&info.pair_handle_key) {
            // This handle is the other end of an already proxied object...
            // Here we need to inform the existing proxy loop that a transfer is going to be
            // initiated, and to where.
            drop(proxied_streams);
            assert_eq!(info.this_handle_key, pair.original_paired);
            log::info!("Send paired proxied {:?} orig_pair={:?}", handle, pair.original_paired);
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
            let stream_ref = stream_ref_receiver.await?;
            match info.handle_type {
                HandleType::Channel => Ok(ZirconHandle::Channel(ChannelHandle { stream_ref })),
                HandleType::Socket(socket_type) => {
                    Ok(ZirconHandle::Socket(SocketHandle { stream_ref, socket_type }))
                }
            }
        } else {
            // This handle (and its pair) is previously unseen... establish a proxy stream for it
            log::trace!("Send proxied {:?}", handle);
            let (tx, rx) = futures::channel::oneshot::channel();
            let rx =
                ProxyTransferInitiationReceiver::new(rx.map_err(move |_| {
                    format_err!("cancelled transfer via send_proxied {:?}", info)
                }));
            let (stream_writer, stream_reader) = conn.alloc_bidi();
            assert!(proxied_streams
                .insert(
                    info.this_handle_key,
                    ProxiedHandle { remove_sender: tx, original_paired: info.pair_handle_key }
                )
                .is_none());
            drop(proxied_streams);
            let stream_ref = StreamRef::Creating(StreamId { id: stream_writer.id() });
            match info.handle_type {
                HandleType::Channel => {
                    crate::proxy::spawn::send(
                        Channel::from_handle(handle).into_proxied()?,
                        rx,
                        stream_writer.into(),
                        stream_reader.into(),
                        stats,
                        Rc::downgrade(&self),
                    )?;
                    Ok(ZirconHandle::Channel(ChannelHandle { stream_ref }))
                }
                HandleType::Socket(socket_type) => {
                    crate::proxy::spawn::send(
                        Socket::from_handle(handle).into_proxied()?,
                        rx,
                        stream_writer.into(),
                        stream_reader.into(),
                        stats,
                        Rc::downgrade(&self),
                    )?;
                    Ok(ZirconHandle::Socket(SocketHandle { stream_ref, socket_type }))
                }
            }
        }
    }

    // Take a received handle description and construct a fidl::Handle that represents it
    // whilst establishing proxies as required
    pub(crate) async fn recv_proxied(
        self: Rc<Self>,
        handle: ZirconHandle,
        conn: AsyncConnection,
        stats: Rc<MessageStats>,
    ) -> Result<Handle, Error> {
        let (tx, rx) = futures::channel::oneshot::channel();
        let debug_id = generate_node_id().0;
        let rx = ProxyTransferInitiationReceiver::new(rx.map_err(move |_| {
            format_err!("cancelled transfer via recv_proxied; debug_id={}", debug_id)
        }));
        let (handle, proxying) = match handle {
            ZirconHandle::Channel(ChannelHandle { stream_ref }) => {
                crate::proxy::spawn::recv(
                    || Channel::create().map_err(Into::into),
                    rx,
                    stream_ref,
                    &conn,
                    stats,
                    Rc::downgrade(&self),
                )
                .await?
            }
            ZirconHandle::Socket(SocketHandle { stream_ref, socket_type }) => {
                let opts = match socket_type {
                    SocketType::Stream => SocketOpts::STREAM,
                    SocketType::Datagram => SocketOpts::DATAGRAM,
                };
                crate::proxy::spawn::recv(
                    || Socket::create(opts).map_err(Into::into),
                    rx,
                    stream_ref,
                    &conn,
                    stats,
                    Rc::downgrade(&self),
                )
                .await?
            }
        };
        if proxying {
            let mut proxied_streams = self.proxied_streams.borrow_mut();
            let info = handle_info(handle.as_handle_ref())?;
            log::trace!(
                "{:?} RECV_PROXIED: {:?} debug_id={} all={:?}",
                self.node_id,
                info,
                debug_id,
                sorted(proxied_streams.keys().map(|x| *x).collect::<Vec<_>>())
            );
            let prior = proxied_streams.insert(
                info.pair_handle_key,
                ProxiedHandle { remove_sender: tx, original_paired: info.this_handle_key },
            );
            assert_eq!(prior.map(|p| p.original_paired), None);
        }
        Ok(handle)
    }

    // Remove a proxied handle from our table.
    // Called by proxy::Proxy::drop.
    pub(crate) fn remove_proxied(self: &Rc<Self>, handle: Handle) -> Result<(), Error> {
        let info = handle_info(handle.as_handle_ref())?;
        log::trace!(
            "{:?} REMOVE_PROXIED: {:?} all={:?}",
            self.node_id,
            info,
            sorted(self.proxied_streams.borrow().keys().map(|x| *x).collect::<Vec<_>>())
        );
        if let Some(removed) = self.proxied_streams.borrow_mut().remove(&info.this_handle_key) {
            assert_eq!(removed.original_paired, info.pair_handle_key);
            let _ = removed.remove_sender.send(RemoveFromProxyTable::Dropped);
        }
        Ok(())
    }

    // Note the endpoint of a transfer that we know about (may complete a transfer operation)
    pub(crate) fn post_transfer(
        &self,
        transfer_key: TransferKey,
        other_end: FoundTransfer,
    ) -> Result<(), Error> {
        let mut pending_transfers = self.pending_transfers.borrow_mut();
        match pending_transfers.insert(transfer_key, PendingTransfer::Complete(other_end)) {
            Some(PendingTransfer::Complete(_)) => bail!("Duplicate transfer received"),
            Some(PendingTransfer::Waiting(w)) => w.wake(),
            None => (),
        }
        Ok(())
    }

    fn poll_find_transfer(
        &self,
        cx: &mut Context<'_>,
        transfer_key: TransferKey,
    ) -> Poll<Result<FoundTransfer, Error>> {
        let mut pending_transfers = self.pending_transfers.borrow_mut();
        if let Some(PendingTransfer::Complete(other_end)) = pending_transfers.remove(&transfer_key)
        {
            Poll::Ready(Ok(other_end))
        } else {
            pending_transfers.insert(transfer_key, PendingTransfer::Waiting(cx.waker().clone()));
            Poll::Pending
        }
    }

    // Lookup a transfer that we're expected to eventually know about
    pub(crate) async fn find_transfer(
        &self,
        transfer_key: TransferKey,
    ) -> Result<FoundTransfer, Error> {
        poll_fn(|cx| self.poll_find_transfer(cx, transfer_key)).await
    }

    // Begin a transfer operation (opposite of find_transfer), publishing an endpoint on the remote
    // nodes transfer table.
    pub(crate) async fn open_transfer(
        self: &Rc<Router>,
        target: NodeId,
        transfer_key: TransferKey,
        handle: Handle,
        link_hint: &Weak<Link>,
    ) -> Result<OpenedTransfer, Error> {
        if target == self.node_id {
            // The target is local: we just file away the handle.
            // Later, find_transfer will find this and we'll collapse away Overnet's involvement and
            // reunite the channel ends.
            let info = handle_info(handle.as_handle_ref())?;
            log::trace!(
                "{:?} OPEN_TRANSFER_REMOVE_PROXIED: {:?} all={:?}",
                self.node_id,
                info,
                sorted(self.proxied_streams.borrow().keys().map(|x| *x).collect::<Vec<_>>())
            );
            if let Some(removed) = self.proxied_streams.borrow_mut().remove(&info.this_handle_key) {
                assert_eq!(removed.original_paired, info.pair_handle_key);
                assert!(removed.remove_sender.send(RemoveFromProxyTable::Dropped).is_ok());
            }
            if let Some(removed) = self.proxied_streams.borrow_mut().remove(&info.pair_handle_key) {
                assert_eq!(removed.original_paired, info.this_handle_key);
                assert!(removed.remove_sender.send(RemoveFromProxyTable::Dropped).is_ok());
            }
            self.post_transfer(transfer_key, FoundTransfer::Fused(handle))?;
            Ok(OpenedTransfer::Fused)
        } else {
            let (writer, reader) =
                self.client_peer(target, link_hint).await?.send_open_transfer(transfer_key).await?;
            Ok(OpenedTransfer::Remote(writer, reader, handle))
        }
    }
}

#[cfg(test)]
pub mod test_util {
    use super::*;
    use std::sync::Once;

    const LOG_LEVEL: log::Level = log::Level::Info;
    const MAX_LOG_LEVEL: log::LevelFilter = log::LevelFilter::Info;

    struct Logger;

    fn short_log_level(level: &log::Level) -> &'static str {
        match *level {
            log::Level::Error => "E",
            log::Level::Warn => "W",
            log::Level::Info => "I",
            log::Level::Debug => "D",
            log::Level::Trace => "T",
        }
    }

    impl log::Log for Logger {
        fn enabled(&self, metadata: &log::Metadata<'_>) -> bool {
            metadata.level() <= LOG_LEVEL
        }

        fn log(&self, record: &log::Record<'_>) {
            if self.enabled(record.metadata()) {
                println!(
                    "{} {} [{}]: {}",
                    record.target(),
                    record
                        .file()
                        .map(|file| {
                            if let Some(line) = record.line() {
                                format!("{}:{}: ", file, line)
                            } else {
                                format!("{}: ", file)
                            }
                        })
                        .unwrap_or(String::new()),
                    short_log_level(&record.level()),
                    record.args()
                );
            }
        }

        fn flush(&self) {}
    }

    static LOGGER: Logger = Logger;
    static START: Once = Once::new();

    fn init() {
        START.call_once(|| {
            log::set_logger(&LOGGER).unwrap();
            log::set_max_level(MAX_LOG_LEVEL);
        })
    }

    pub fn run<F, Fut>(f: F)
    where
        F: 'static + Send + FnOnce() -> Fut,
        Fut: Future<Output = ()> + 'static,
    {
        const TEST_TIMEOUT_MS: u32 = 60_000;
        init();
        timebomb::timeout_ms(move || crate::runtime::run(f()), TEST_TIMEOUT_MS)
    }

    #[cfg(not(target_os = "fuchsia"))]
    fn temp_file_containing(bytes: &[u8]) -> Box<dyn AsRef<std::path::Path>> {
        let mut path = tempfile::NamedTempFile::new().unwrap();
        use std::io::Write;
        path.write_all(bytes).unwrap();
        Box::new(path)
    }

    pub fn test_router_options() -> RouterOptions {
        let options = RouterOptions::new();
        #[cfg(target_os = "fuchsia")]
        let options = options
            .set_quic_server_cert_file(Box::new("/pkg/data/cert.crt".to_string()))
            .set_quic_server_key_file(Box::new("/pkg/data/cert.key".to_string()));
        #[cfg(not(target_os = "fuchsia"))]
        let options = options
            .set_quic_server_cert_file(temp_file_containing(include_bytes!(
                "../../../../../../third_party/rust-mirrors/quiche/examples/cert.crt"
            )))
            .set_quic_server_key_file(temp_file_containing(include_bytes!(
                "../../../../../../third_party/rust-mirrors/quiche/examples/cert.key"
            )));
        options
    }
}

#[cfg(test)]
mod tests {

    use super::test_util::*;
    use super::*;
    use crate::future_help::log_errors;
    use crate::runtime::{spawn, wait_until};
    use futures::{executor::block_on, select};
    use std::time::{Duration, Instant};

    #[test]
    fn no_op() {
        run(|| async move {
            Router::new(test_router_options()).unwrap();
            assert_eq!(
                Router::new(test_router_options().set_node_id(1.into()),).unwrap().node_id.0,
                1
            );
        })
    }

    struct TwoNode {
        router1: Rc<Router>,
        router2: Rc<Router>,
    }

    fn forward(sender: Rc<Link>, receiver: Rc<Link>) {
        spawn(log_errors(
            async move {
                let mut frame = [0u8; 2048];
                while let Some(n) = sender.next_send(&mut frame).await? {
                    receiver.received_packet(&mut frame[..n]).await?;
                }
                Ok(())
            },
            "forwarder failed",
        ));
    }

    async fn register_test_service(
        router: &Rc<Router>,
        service: &str,
    ) -> futures::channel::oneshot::Receiver<(NodeId, Channel)> {
        use std::sync::Mutex;
        let (send, recv) = futures::channel::oneshot::channel();
        struct TestService(Mutex<Option<futures::channel::oneshot::Sender<(NodeId, Channel)>>>);
        impl fidl_fuchsia_overnet::ServiceProviderProxyInterface for TestService {
            fn connect_to_service(
                &self,
                chan: fidl::Channel,
                connection_info: fidl_fuchsia_overnet::ConnectionInfo,
            ) -> std::result::Result<(), fidl::Error> {
                self.0
                    .lock()
                    .unwrap()
                    .take()
                    .unwrap()
                    .send((connection_info.peer.unwrap().id.into(), chan))
                    .unwrap();
                Ok(())
            }
        }
        router
            .service_map()
            .register_service(service.to_string(), Box::new(TestService(Mutex::new(Some(send)))))
            .await;
        recv
    }

    impl TwoNode {
        async fn new() -> Self {
            let router1 = Router::new(test_router_options()).unwrap();
            let router2 = Router::new(test_router_options()).unwrap();
            let link1 = router1.new_link(router2.node_id).await.unwrap();
            let link2 = router2.new_link(router1.node_id).await.unwrap();
            forward(link1.clone(), link2.clone());
            forward(link2, link1);

            TwoNode { router1, router2 }
        }
    }

    #[test]
    fn no_op_env() {
        run(|| async move {
            TwoNode::new().await;
        })
    }

    #[test]
    fn create_stream() {
        run(|| async move {
            let env = TwoNode::new().await;
            let (_, p) = fidl::Channel::create().unwrap();
            let s = register_test_service(&env.router2, "hello world").await;
            env.router1.connect_to_service(env.router2.node_id, "hello world", p).await.unwrap();
            let (node_id, _) = s.await.unwrap();
            assert_eq!(node_id, env.router1.node_id);
        })
    }

    #[test]
    fn send_datagram_immediately() {
        run(|| async move {
            let env = TwoNode::new().await;
            let (c, p) = fidl::Channel::create().unwrap();
            let s = register_test_service(&env.router2, "hello world").await;
            env.router1.connect_to_service(env.router2.node_id, "hello world", p).await.unwrap();
            let (node_id, s) = s.await.unwrap();
            assert_eq!(node_id, env.router1.node_id);
            let c = fidl::AsyncChannel::from_channel(c).unwrap();
            let s = fidl::AsyncChannel::from_channel(s).unwrap();
            c.write(&[1, 2, 3, 4, 5], &mut Vec::new()).unwrap();
            let mut buf = fidl::MessageBuf::new();
            s.recv_msg(&mut buf).await.unwrap();
            assert_eq!(buf.n_handles(), 0);
            assert_eq!(buf.bytes(), &[1, 2, 3, 4, 5]);
        })
    }

    #[test]
    fn ping_pong() {
        run(|| async move {
            let env = TwoNode::new().await;
            let (c, p) = fidl::Channel::create().unwrap();
            let s = register_test_service(&env.router2, "hello world").await;
            env.router1.connect_to_service(env.router2.node_id, "hello world", p).await.unwrap();
            let (node_id, s) = s.await.unwrap();
            assert_eq!(node_id, env.router1.node_id);

            let c = fidl::AsyncChannel::from_channel(c).unwrap();
            let s = fidl::AsyncChannel::from_channel(s).unwrap();

            println!("send ping");
            c.write(&[1, 2, 3, 4, 5], &mut Vec::new()).unwrap();
            println!("receive ping");
            let mut buf = fidl::MessageBuf::new();
            s.recv_msg(&mut buf).await.unwrap();
            assert_eq!(buf.n_handles(), 0);
            assert_eq!(buf.bytes(), &[1, 2, 3, 4, 5]);

            println!("send pong");
            s.write(&[9, 8, 7, 6, 5, 4, 3, 2, 1], &mut Vec::new()).unwrap();
            println!("receive pong");
            let mut buf = fidl::MessageBuf::new();
            c.recv_msg(&mut buf).await.unwrap();
            assert_eq!(buf.n_handles(), 0);
            assert_eq!(buf.bytes(), &[9, 8, 7, 6, 5, 4, 3, 2, 1]);
        })
    }

    async fn assert_avail(r: &mut futures::channel::mpsc::Receiver<()>, mut n: usize) {
        while n > 0 {
            r.next().await.unwrap();
            n -= 1;
        }
        select! {
            _ = r.next().fuse() => panic!("Unexpected output"),
            _ = wait_until(Instant::now() + Duration::from_secs(1)).fuse() => ()
        }
    }

    #[test]
    fn concurrent_list_peer_calls_will_error() {
        run(|| async move {
            let n = Router::new(test_router_options()).unwrap();
            let (tx, mut rx) = futures::channel::mpsc::channel(1);
            let mut c1 = tx.clone();
            let mut c2 = tx.clone();
            let mut c3 = tx.clone();
            assert_avail(&mut rx, 0).await;
            n.list_peers(Box::new(move |_| block_on(c1.send(())).unwrap())).await.unwrap();
            assert_avail(&mut rx, 1).await;
            n.list_peers(Box::new(move |_| block_on(c2.send(())).unwrap())).await.unwrap();
            assert_avail(&mut rx, 0).await;
            n.list_peers(Box::new(move |_| block_on(c3.send(())).unwrap()))
                .await
                .expect_err("Concurrent list peers should fail");
            assert_avail(&mut rx, 0).await;
        })
    }

    #[test]
    #[cfg(not(target_os = "fuchsia"))]
    fn initial_greeting_packet() {
        use crate::coding::decode_fidl;
        use crate::stream_framer::StreamDeframer;
        use fidl_fuchsia_overnet_protocol::StreamSocketGreeting;
        run(|| async move {
            let n = Router::new(test_router_options()).unwrap();
            let node_id = n.node_id();
            let (c, s) = fidl::Socket::create(fidl::SocketOpts::STREAM).unwrap();
            let mut c = fidl::AsyncSocket::from_socket(c).unwrap();
            n.attach_socket_link(
                s,
                fidl_fuchsia_overnet::SocketLinkOptions {
                    connection_label: Some("test".to_string()),
                    bytes_per_second: None,
                },
            )
            .unwrap();

            let mut deframer = StreamDeframer::new();
            let mut buf = [0u8; 1024];
            let mut greeting_bytes = loop {
                let n = c.read(&mut buf).await.unwrap();
                deframer.queue_recv(&buf[..n]);
                if let Some(greeting) = deframer.next_incoming_frame() {
                    break greeting;
                }
            };
            let greeting = decode_fidl::<StreamSocketGreeting>(greeting_bytes.as_mut()).unwrap();
            assert_eq!(greeting.magic_string, Some("OVERNET SOCKET LINK".to_string()));
            assert_eq!(greeting.node_id, Some(node_id.into()));
            assert_eq!(greeting.connection_label, Some("test".to_string()));
        })
    }

    #[test]
    #[cfg(not(target_os = "fuchsia"))]
    fn attach_with_zero_bytes_per_second() {
        run(|| async move {
            let n = Router::new(test_router_options()).unwrap();
            let (c, s) = fidl::Socket::create(fidl::SocketOpts::STREAM).unwrap();
            n.attach_socket_link(
                s,
                fidl_fuchsia_overnet::SocketLinkOptions {
                    connection_label: Some("test".to_string()),
                    bytes_per_second: Some(0),
                },
            )
            .expect_err("bytes_per_second == 0 should fail");
            drop(c);
        })
    }
}
