// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod framed_stream;

pub(crate) use self::framed_stream::{
    FrameType, FramedStreamReader, FramedStreamWriter, MessageStats,
};
use crate::{
    coding::{self, decode_fidl_with_context, encode_fidl_with_context},
    future_help::Observer,
    labels::{ConnectionId, Endpoint, NodeId, TransferKey},
    link::{LinkRouting, OutputQueue, RoutingDestination, RoutingTarget},
    router::{ForwardingTable, FoundTransfer, Router},
};
use anyhow::{bail, format_err, Context as _, Error};
use async_utils::mutex_ticket::MutexTicket;
use cutex::{CutexGuard, CutexTicket};
use fidl::{Channel, HandleBased};
use fidl_fuchsia_overnet::ConnectionInfo;
use fidl_fuchsia_overnet_protocol::{
    ChannelHandle, ConfigRequest, ConfigResponse, ConnectToService, ConnectToServiceOptions,
    OpenTransfer, PeerConnectionDiagnosticInfo, PeerDescription, PeerMessage, PeerReply, StreamId,
    ZirconHandle,
};
use fuchsia_async::{Task, TimeoutExt};
use futures::{
    channel::{mpsc, oneshot},
    lock::Mutex,
    prelude::*,
    ready,
};
use quic::{
    AsyncConnection, AsyncQuicStreamReader, AsyncQuicStreamWriter, ConnState, StreamProperties,
};
use std::{
    convert::TryInto,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Weak,
    },
    task::{Context, Poll},
    time::Duration,
};

#[derive(Debug)]
struct Config {}

impl Config {
    fn negotiate(_request: ConfigRequest) -> (Self, ConfigResponse) {
        (Config {}, ConfigResponse::EMPTY)
    }

    fn from_response(_response: ConfigResponse) -> Self {
        Config {}
    }
}

#[derive(Debug)]
enum ClientPeerCommand {
    ConnectToService(ConnectToService),
    OpenTransfer(u64, TransferKey, oneshot::Sender<()>),
}

#[derive(Default)]
pub struct PeerConnStats {
    pub config: MessageStats,
    pub connect_to_service: MessageStats,
    pub update_node_description: MessageStats,
    pub update_link_status: MessageStats,
    pub update_link_status_ack: MessageStats,
    pub open_transfer: MessageStats,
    pub ping: MessageStats,
    pub pong: MessageStats,
}

#[derive(Clone)]
enum PeerConnInner {
    Quic(Arc<AsyncConnection>),
    Circuit(circuit::Connection),
}

impl PeerConnInner {
    /// Convert a PeerConnInner to a PeerConnRefInner
    fn into_ref(&self) -> PeerConnRefInner<'_> {
        match self {
            Self::Quic(p) => PeerConnRefInner::Quic(p),
            Self::Circuit(p) => PeerConnRefInner::Circuit(p),
        }
    }
}

#[derive(Clone)]
pub(crate) struct PeerConn {
    inner: PeerConnInner,
    node_id: NodeId,
}

impl std::fmt::Debug for PeerConn {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.as_ref().fmt(f)
    }
}

impl PeerConn {
    pub fn from_quic(conn: Arc<AsyncConnection>, node_id: NodeId) -> Self {
        PeerConn { inner: PeerConnInner::Quic(conn), node_id }
    }

    pub fn from_circuit(conn: circuit::Connection, node_id: NodeId) -> Self {
        PeerConn { inner: PeerConnInner::Circuit(conn), node_id }
    }

    pub fn quic_conn(&self) -> Option<Arc<AsyncConnection>> {
        match &self.inner {
            PeerConnInner::Quic(conn) => Some(Arc::clone(&conn)),
            PeerConnInner::Circuit(_) => None,
        }
    }

    pub fn as_ref(&self) -> PeerConnRef<'_> {
        PeerConnRef { inner: self.inner.into_ref(), node_id: self.node_id }
    }

    pub async fn stats(&self) -> Option<quiche::Stats> {
        match &self.inner {
            PeerConnInner::Quic(conn) => Some(conn.stats().await),
            PeerConnInner::Circuit(_) => None,
        }
    }

    pub async fn is_established(&self) -> bool {
        match &self.inner {
            PeerConnInner::Quic(conn) => conn.is_established().await,
            // For QUIC, is_established indicates we are not in an intermediate phase of the
            // handshake procedure. Circuits don't really have a rigid handshake procedure; as soon
            // as we're returned a writer it's ready to use and that's all there is to know.
            PeerConnInner::Circuit(_) => true,
        }
    }

    pub async fn close(&self) {
        match &self.inner {
            PeerConnInner::Quic(conn) => conn.close().await,
            // TODO: Figure out what this should do.
            PeerConnInner::Circuit(_) => (),
        }
    }

    pub async fn recv(&self, packet: &mut [u8]) -> Result<(), Error> {
        match &self.inner {
            PeerConnInner::Quic(conn) => conn.recv(packet).await,
            PeerConnInner::Circuit(_) => {
                unreachable!("Old link protocol tried to feed a packet to a circuit!")
            }
        }
    }

    pub fn node_id(&self) -> NodeId {
        self.node_id
    }
}

#[derive(Clone, Copy)]
enum PeerConnRefInner<'a> {
    Quic(&'a Arc<AsyncConnection>),
    Circuit(&'a circuit::Connection),
}

impl PeerConnRefInner<'_> {
    /// Convert a PeerConnRefInner to a PeerConnInner
    fn into_owned(&self) -> PeerConnInner {
        match self {
            Self::Quic(p) => PeerConnInner::Quic(Arc::clone(p)),
            Self::Circuit(p) => PeerConnInner::Circuit((*p).clone()),
        }
    }
}

impl<'a> std::fmt::Debug for PeerConnRefInner<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            PeerConnRefInner::Quic(conn) => write!(f, "{}", conn.trace_id()),
            PeerConnRefInner::Circuit(_) => write!(f, "<circuit>"),
        }
    }
}

#[derive(Clone, Copy)]
pub(crate) struct PeerConnRef<'a> {
    inner: PeerConnRefInner<'a>,
    node_id: NodeId,
}

impl<'a> std::fmt::Debug for PeerConnRef<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "PeerConn({}; {:?})", self.node_id.0, self.inner)
    }
}

impl<'a> PeerConnRef<'a> {
    pub fn from_quic(conn: &'a Arc<AsyncConnection>, node_id: NodeId) -> Self {
        PeerConnRef { inner: PeerConnRefInner::Quic(conn), node_id }
    }

    pub fn from_circuit(conn: &'a circuit::Connection, node_id: NodeId) -> Self {
        PeerConnRef { inner: PeerConnRefInner::Circuit(conn), node_id }
    }

    pub fn into_peer_conn(&self) -> PeerConn {
        PeerConn { inner: self.inner.into_owned(), node_id: self.node_id }
    }

    pub fn endpoint(&self) -> Endpoint {
        match &self.inner {
            PeerConnRefInner::Quic(conn) => conn.endpoint(),
            PeerConnRefInner::Circuit(conn) => {
                if conn.is_client() {
                    Endpoint::Client
                } else {
                    Endpoint::Server
                }
            }
        }
    }

    pub fn peer_node_id(&self) -> NodeId {
        self.node_id
    }

    pub async fn alloc_uni(&self) -> Result<FramedStreamWriter, Error> {
        match &self.inner {
            PeerConnRefInner::Quic(conn) => {
                Ok(FramedStreamWriter::from_quic(conn.alloc_uni(), self.node_id))
            }
            PeerConnRefInner::Circuit(conn) => {
                let (circuit_reader, writer) = circuit::stream::stream();
                let (_reader, circuit_writer) = circuit::stream::stream();
                let id = conn.alloc_stream(circuit_reader, circuit_writer).await?;
                Ok(FramedStreamWriter::from_circuit(writer, id, (*conn).clone(), self.node_id))
            }
        }
    }

    pub async fn alloc_bidi(&self) -> Result<(FramedStreamWriter, FramedStreamReader), Error> {
        match &self.inner {
            PeerConnRefInner::Quic(conn) => {
                let (w, r) = conn.alloc_bidi();
                Ok((
                    FramedStreamWriter::from_quic(w, self.node_id),
                    FramedStreamReader::from_quic(r, self.node_id),
                ))
            }
            PeerConnRefInner::Circuit(conn) => {
                let (circuit_reader, writer) = circuit::stream::stream();
                let (reader, circuit_writer) = circuit::stream::stream();
                let id = conn.alloc_stream(circuit_reader, circuit_writer).await?;
                Ok((
                    FramedStreamWriter::from_circuit(writer, id, (*conn).clone(), self.node_id),
                    FramedStreamReader::from_circuit(reader, (*conn).clone(), self.node_id),
                ))
            }
        }
    }

    pub async fn bind_uni_id(&self, id: u64) -> Result<FramedStreamReader, Error> {
        match &self.inner {
            PeerConnRefInner::Quic(conn) => {
                Ok(FramedStreamReader::from_quic(conn.bind_uni_id(id), self.node_id))
            }
            PeerConnRefInner::Circuit(conn) => Ok(FramedStreamReader::from_circuit(
                conn.bind_stream(id)
                    .await
                    .ok_or_else(|| format_err!("Stream id {} unavailable", id))?
                    .0,
                (*conn).clone(),
                self.node_id,
            )),
        }
    }

    pub async fn bind_id(
        &self,
        id: u64,
    ) -> Result<(FramedStreamWriter, FramedStreamReader), Error> {
        match &self.inner {
            PeerConnRefInner::Quic(conn) => {
                let (w, r) = conn.bind_id(id);
                Ok((
                    FramedStreamWriter::from_quic(w, self.node_id),
                    FramedStreamReader::from_quic(r, self.node_id),
                ))
            }
            PeerConnRefInner::Circuit(conn) => {
                let (r, w) = conn
                    .bind_stream(id)
                    .await
                    .ok_or_else(|| format_err!("Stream id {} unavailable", id))?;
                Ok((
                    FramedStreamWriter::from_circuit(w, id, (*conn).clone(), self.node_id),
                    FramedStreamReader::from_circuit(r, (*conn).clone(), self.node_id),
                ))
            }
        }
    }
}

pub(crate) struct Peer {
    endpoint: Endpoint,
    conn_id: ConnectionId,
    /// The QUIC connection itself
    conn: PeerConn,
    commands: Option<mpsc::Sender<ClientPeerCommand>>,
    conn_stats: Arc<PeerConnStats>,
    channel_proxy_stats: Arc<MessageStats>,
    _task: Task<()>,
    shutdown: AtomicBool,
}

impl std::fmt::Debug for Peer {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.debug_id().fmt(f)
    }
}

/// Future to perform one send from a peer to its current link.
struct OneSend<'a> {
    /// Link upon which to send.
    link: &'a LinkRouting,
    /// QUIC connection that is forming frames to send.
    conn: &'a Arc<AsyncConnection>,
    /// Node this peer communicates with.
    node_id: NodeId,
    /// Current lock state.
    state: OneSendState<'a>,
}

/// Lock state to perform one send. This is trickier than I'd like.
/// To send we need to acquire both of the link and then the connection locks (in order).
/// The link lock needs to be acquired *only when* the link is ready to send a frame.
/// The connection lock may or may not yield a frame to be sent.
/// We don't want to occupy the link lock for longer than is necessary however, otherwise
/// other peers will be starved.
/// So:
///   1. we acquire a ticket to wait for the link lock (in a sendable state)
///   2. once we have the link locked, we try the same for the QUIC connection
///   3. once acquired, we try to pull a frame from it
///      on success - we update the link state and relinquish the locks
///      on failure - we're queued as a waiter against the connection for fresh data
///                   and we also relinquish the locks; when the connection is believed
///                   to have fresh data, we'll be awoken and enter stage 1 again.
#[derive(Debug)]
enum OneSendState<'a> {
    Idle,
    LockingLink(CutexTicket<'a, 'static, OutputQueue>),
    LockingConn(CutexGuard<'a, OutputQueue>, MutexTicket<'a, ConnState>),
}

impl<'a> OneSend<'a> {
    fn poll(&mut self, ctx: &mut Context<'_>) -> Poll<Result<(), Error>> {
        match std::mem::replace(&mut self.state, OneSendState::Idle) {
            OneSendState::Idle => self.poll_idle(ctx),
            OneSendState::LockingLink(poll_cutex) => self.poll_locking_link(ctx, poll_cutex),
            OneSendState::LockingConn(cutex_guard, poll_mutex) => {
                self.poll_locking_conn(ctx, cutex_guard, poll_mutex)
            }
        }
    }

    /// We're being polled and we aren't currently acquiring a lock... begin to acquire the
    /// link lock and progress to the next state.
    fn poll_idle(&mut self, ctx: &mut Context<'_>) -> Poll<Result<(), Error>> {
        self.poll_locking_link(ctx, self.link.new_message_send_ticket())
    }

    /// We're being polled while trying to acquire the link lock - poll against that
    /// on ready   - move on to polling the connection lock
    /// on pending - wait longer to lock the link
    fn poll_locking_link(
        &mut self,
        ctx: &mut Context<'_>,
        mut poll_cutex: CutexTicket<'a, 'static, OutputQueue>,
    ) -> Poll<Result<(), Error>> {
        match poll_cutex.poll(ctx) {
            Poll::Pending => {
                self.state = OneSendState::LockingLink(poll_cutex);
                Poll::Pending
            }
            Poll::Ready(cutex_guard) => {
                self.poll_locking_conn(ctx, cutex_guard, self.conn.poll_lock_state())
            }
        }
    }

    /// We're being polled while holding the link lock and attempting to acquire the connection
    /// lock.
    /// Poll the connection lock.
    /// If we acquire it, try to pull a frame and send it via the link, then relinquish both locks
    /// and move back to being idle.
    /// If we cannot yet acquire the connection lock continue trying to do so.
    fn poll_locking_conn(
        &mut self,
        ctx: &mut Context<'_>,
        mut cutex_guard: CutexGuard<'a, OutputQueue>,
        mut poll_mutex: MutexTicket<'a, ConnState>,
    ) -> Poll<Result<(), Error>> {
        match poll_mutex.poll(ctx) {
            Poll::Pending => {
                self.state = OneSendState::LockingConn(cutex_guard, poll_mutex);
                Poll::Pending
            }
            Poll::Ready(mut mutex_guard) => {
                let mut send = cutex_guard.send(self.routing_target())?;
                if let Some(n) = ready!(mutex_guard.poll_send(ctx, send.buffer()))? {
                    send.commit(n);
                    Poll::Ready(Ok(()))
                } else {
                    Poll::Ready(Err(format_err!(
                        "QUIC connection {:?} closed",
                        self.conn.trace_id()
                    )))
                }
            }
        }
    }

    fn routing_target(&self) -> RoutingTarget {
        RoutingTarget {
            src: self.link.own_node_id(),
            dst: RoutingDestination::Message(self.node_id),
        }
    }
}

/// Task to send frames produced by a peer to a designated link.
/// Effectively an infinite loop around `OneSend`.
async fn peer_to_link(conn: Arc<AsyncConnection>, node_id: NodeId, link: Arc<LinkRouting>) {
    loop {
        let mut one_send =
            OneSend { link: &*link, conn: &conn, node_id, state: OneSendState::Idle };
        if let Err(e) = future::poll_fn(move |ctx| one_send.poll(ctx)).await {
            tracing::warn!(
                "Sender for {:?} on link {:?} failed: {:?}",
                conn.trace_id(),
                link.debug_id(),
                e
            );
            break;
        }
    }
}

/// Error from the run loops for a peer (client or server) - captures a little semantic detail
/// to help direct reactions to this peer disappearing.
#[derive(Debug)]
enum RunnerError {
    NoRouteToPeer,
    RouterGone,
    ConnectionClosed,
    BadFrameType(FrameType),
    HandshakeError(Error),
    ServiceError(Error),
}

/// Returns the next link a peer should send to (as forwarding tables change).
/// If a peer becomes unroutable, returns an error (after verifying for a short time).
async fn next_link(
    router: &Weak<Router>,
    peer: NodeId,
    observer: &mut Observer<ForwardingTable>,
) -> Result<Arc<LinkRouting>, RunnerError> {
    // Helper: pulls the next forwarding table, and returns Some(link) if the peer is routable,
    // or None if it is not.
    async fn maybe_next_link(
        router: &Weak<Router>,
        peer: NodeId,
        observer: &mut Observer<ForwardingTable>,
    ) -> Result<Option<Arc<LinkRouting>>, RunnerError> {
        let get_router = move || Weak::upgrade(&router).ok_or(RunnerError::RouterGone);
        let forwarding_table = observer.next().await.ok_or(RunnerError::RouterGone)?;
        if let Some(node_link_id) = forwarding_table.route_for(peer) {
            return Ok(get_router()?.get_link(node_link_id).await);
        }
        Ok(None)
    }

    if let Some(new_link) = maybe_next_link(router, peer, observer).await? {
        return Ok(new_link);
    }
    // Route flap prevention: if we observe no new route, keep looking for a short time to see if
    // one reappears before dropping the peer (and consequently any channels it carries).
    async move {
        loop {
            if let Some(new_link) = maybe_next_link(router, peer, observer).await? {
                return Ok(new_link);
            }
        }
    }
    .on_timeout(Duration::from_secs(5), || Err(RunnerError::NoRouteToPeer))
    .await
}

/// Ensure connectivity to a peer.
/// Update the peer with a new link whenever needed.
/// Fail if there's no connectivity to a peer.
async fn check_connectivity(
    router: Weak<Router>,
    conn: PeerConn,
    mut wait_for_route: Option<oneshot::Sender<()>>,
) -> Result<(), RunnerError> {
    let mut sender_and_current_link: Option<(Task<()>, Arc<LinkRouting>)> = None;
    let mut observer = Weak::upgrade(&router)
        .ok_or_else(|| RunnerError::RouterGone)?
        .new_forwarding_table_observer();
    loop {
        let new_link = next_link(&router, conn.node_id(), &mut observer).await?;
        if sender_and_current_link
            .as_ref()
            .map(|sender_and_current_link| !Arc::ptr_eq(&sender_and_current_link.1, &new_link))
            .unwrap_or(true)
        {
            tracing::trace!(
                "Peer {:?} route set to {:?} from {:?}",
                conn,
                new_link.debug_id(),
                sender_and_current_link.as_ref().map(|s_and_l| s_and_l.1.debug_id())
            );
            if let Some(sender) = wait_for_route.take() {
                let _ = sender.send(());
            }
            sender_and_current_link = Some((
                Task::spawn(peer_to_link(
                    conn.quic_conn().expect("Should not run link updates for circuit connection!"),
                    conn.node_id(),
                    new_link.clone(),
                )),
                new_link,
            ));
        }
    }
}

impl Peer {
    pub(crate) fn node_id(&self) -> NodeId {
        self.conn.node_id()
    }

    pub(crate) fn endpoint(&self) -> Endpoint {
        self.endpoint
    }

    pub(crate) fn debug_id(&self) -> impl std::fmt::Debug + std::cmp::PartialEq {
        (self.node_id(), self.endpoint, self.conn_id)
    }

    /// Construct a new client peer - spawns tasks to handle making control stream requests, and
    /// publishing link metadata
    #[cfg_attr(not(circuit), allow(dead_code))]
    pub(crate) fn new_circuit_client(
        conn: circuit::Connection,
        conn_stream_writer: circuit::stream::Writer,
        conn_stream_reader: circuit::stream::Reader,
        service_observer: Observer<Vec<String>>,
        router: &Arc<Router>,
    ) -> Result<Arc<Self>, Error> {
        let conn_id = ConnectionId::new();
        let node_id =
            NodeId::from_circuit_string(conn.from()).map_err(|_| format_err!("Invalid node ID"))?;
        tracing::trace!(node_id = router.node_id().0, peer = node_id.0, ?conn_id, "NEW CLIENT",);
        let (command_sender, command_receiver) = mpsc::channel(1);
        let conn_stats = Arc::new(PeerConnStats::default());
        Ok(Arc::new(Self {
            endpoint: Endpoint::Client,
            conn_id,
            commands: Some(command_sender.clone()),
            conn_stats: conn_stats.clone(),
            channel_proxy_stats: Arc::new(MessageStats::default()),
            shutdown: AtomicBool::new(false),
            _task: Task::spawn(Peer::runner(
                Endpoint::Client,
                Arc::downgrade(router),
                conn_id,
                client_conn_stream(
                    Arc::downgrade(router),
                    node_id,
                    RawConnection::Circuit(conn_stream_writer, conn_stream_reader, conn.clone()),
                    command_receiver,
                    service_observer,
                    conn_stats,
                ),
            )),
            conn: PeerConn::from_circuit(conn, node_id),
        }))
    }

    /// Construct a new server peer - spawns tasks to handle responding to control stream requests
    #[cfg_attr(not(circuit), allow(dead_code))]
    pub(crate) async fn new_circuit_server(
        conn: circuit::Connection,
        router: &Arc<Router>,
    ) -> Result<Arc<Self>, Error> {
        let conn_id = ConnectionId::new();
        let node_id =
            NodeId::from_circuit_string(conn.from()).map_err(|_| format_err!("Invalid node ID"))?;
        tracing::trace!(node_id = router.node_id().0, peer = node_id.0, ?conn_id, "NEW SERVER",);
        let (conn_stream_reader, conn_stream_writer) = conn
            .bind_stream(0)
            .await
            .ok_or_else(|| format_err!("Could not establish connection"))?;
        let conn_stats = Arc::new(PeerConnStats::default());
        let channel_proxy_stats = Arc::new(MessageStats::default());
        Ok(Arc::new(Self {
            endpoint: Endpoint::Server,
            conn_id,
            commands: None,
            conn_stats: conn_stats.clone(),
            channel_proxy_stats: channel_proxy_stats.clone(),
            shutdown: AtomicBool::new(false),
            _task: Task::spawn(Peer::runner(
                Endpoint::Server,
                Arc::downgrade(router),
                conn_id,
                server_conn_stream(
                    node_id,
                    RawConnection::Circuit(conn_stream_writer, conn_stream_reader, conn.clone()),
                    Arc::downgrade(router),
                    conn_stats,
                    channel_proxy_stats,
                ),
            )),
            conn: PeerConn::from_circuit(conn, node_id),
        }))
    }

    /// Construct a new client peer - spawns tasks to handle making control stream requests, and
    /// publishing link metadata
    pub(crate) fn new_client(
        node_id: NodeId,
        local_node_id: NodeId,
        conn_id: ConnectionId,
        config: &mut quiche::Config,
        service_observer: Observer<Vec<String>>,
        router: &Arc<Router>,
        wait_for_route: Option<oneshot::Sender<()>>,
    ) -> Result<Arc<Self>, Error> {
        tracing::trace!(node_id = router.node_id().0, peer = node_id.0, ?conn_id, "NEW CLIENT",);
        let (command_sender, command_receiver) = mpsc::channel(1);
        let conn = AsyncConnection::connect(
            None,
            &quiche::ConnectionId::from_ref(&conn_id.to_array()),
            local_node_id.to_ipv6_repr(),
            config,
        )?;
        let conn_stats = Arc::new(PeerConnStats::default());
        let (conn_stream_writer, conn_stream_reader) = conn.alloc_bidi();
        assert_eq!(conn_stream_writer.id(), 0);
        Ok(Arc::new(Self {
            endpoint: Endpoint::Client,
            conn_id,
            commands: Some(command_sender.clone()),
            conn_stats: conn_stats.clone(),
            channel_proxy_stats: Arc::new(MessageStats::default()),
            shutdown: AtomicBool::new(false),
            _task: Task::spawn(Peer::runner(
                Endpoint::Client,
                Arc::downgrade(router),
                conn_id,
                futures::future::try_join(
                    check_connectivity(
                        Arc::downgrade(router),
                        PeerConn::from_quic(conn.clone(), node_id),
                        wait_for_route,
                    ),
                    client_conn_stream(
                        Arc::downgrade(router),
                        node_id,
                        RawConnection::Quic(conn_stream_writer, conn_stream_reader),
                        command_receiver,
                        service_observer,
                        conn_stats,
                    ),
                )
                .map_ok(drop),
            )),
            conn: PeerConn::from_quic(conn, node_id),
        }))
    }

    /// Construct a new server peer - spawns tasks to handle responding to control stream requests
    pub(crate) fn new_server(
        node_id: NodeId,
        local_node_id: NodeId,
        conn_id: ConnectionId,
        config: &mut quiche::Config,
        router: &Arc<Router>,
    ) -> Result<Arc<Self>, Error> {
        tracing::trace!(node_id = router.node_id().0, peer = node_id.0, ?conn_id, "NEW SERVER",);
        let conn = AsyncConnection::accept(
            &quiche::ConnectionId::from_ref(&conn_id.to_array()),
            local_node_id.to_ipv6_repr(),
            config,
        )?;
        let conn_stats = Arc::new(PeerConnStats::default());
        let (conn_stream_writer, conn_stream_reader) = conn.bind_id(0);
        let channel_proxy_stats = Arc::new(MessageStats::default());
        Ok(Arc::new(Self {
            endpoint: Endpoint::Server,
            conn_id,
            commands: None,
            conn_stats: conn_stats.clone(),
            channel_proxy_stats: channel_proxy_stats.clone(),
            shutdown: AtomicBool::new(false),
            _task: Task::spawn(Peer::runner(
                Endpoint::Server,
                Arc::downgrade(router),
                conn_id,
                futures::future::try_join(
                    check_connectivity(
                        Arc::downgrade(router),
                        PeerConn::from_quic(conn.clone(), node_id),
                        None,
                    ),
                    server_conn_stream(
                        node_id,
                        RawConnection::Quic(conn_stream_writer, conn_stream_reader),
                        Arc::downgrade(router),
                        conn_stats,
                        channel_proxy_stats,
                    ),
                )
                .map_ok(drop),
            )),
            conn: PeerConn::from_quic(conn, node_id),
        }))
    }

    async fn runner(
        endpoint: Endpoint,
        router: Weak<Router>,
        conn_id: ConnectionId,
        f: impl Future<Output = Result<(), RunnerError>>,
    ) {
        let result = f.await;
        let get_router_node_id = || {
            Weak::upgrade(&router).map(|r| format!("{:?}", r.node_id())).unwrap_or_else(String::new)
        };
        if let Err(RunnerError::NoRouteToPeer) = &result {
            tracing::trace!(
                node_id = %get_router_node_id(),
                conn = ?conn_id,
                ?endpoint,
                "runner lost route to peer",
            );
        } else if let Err(e) = &result {
            tracing::error!(
                node_id = %get_router_node_id(),
                conn = ?conn_id,
                ?endpoint,
                "runner error: {:?}",
                e
            );
        } else {
            tracing::trace!(
                node_id = %get_router_node_id(),
                conn = ?conn_id,
                ?endpoint,
                "finished successfully",
            );
        }
        if let Some(router) = Weak::upgrade(&router) {
            router.remove_peer(conn_id, matches!(result, Err(RunnerError::NoRouteToPeer))).await;
        }
    }

    pub async fn shutdown(&self) {
        self.shutdown.store(true, Ordering::Release);
        self.conn.close().await
    }

    pub async fn receive_frame(&self, frame: &mut [u8]) -> Result<(), Error> {
        self.conn.recv(frame).await
    }

    pub async fn new_stream(
        &self,
        service: &str,
        chan: Channel,
        router: &Arc<Router>,
    ) -> Result<(), Error> {
        if let ZirconHandle::Channel(ChannelHandle { stream_ref, rights }) = router
            .send_proxied(
                chan.into_handle(),
                self.peer_conn_ref(),
                self.channel_proxy_stats.clone(),
            )
            .await?
        {
            self.commands
                .as_ref()
                .unwrap()
                .clone()
                .send(ClientPeerCommand::ConnectToService(ConnectToService {
                    service_name: service.to_string(),
                    stream_ref,
                    rights,
                    options: ConnectToServiceOptions::EMPTY,
                }))
                .await?;
            Ok(())
        } else {
            unreachable!();
        }
    }

    pub async fn send_open_transfer(
        &self,
        transfer_key: TransferKey,
    ) -> Option<(FramedStreamWriter, FramedStreamReader)> {
        let io = self.peer_conn_ref().alloc_bidi().await.ok()?;
        let (tx, rx) = oneshot::channel();
        self.commands
            .as_ref()
            .unwrap()
            .clone()
            .send(ClientPeerCommand::OpenTransfer(io.0.id(), transfer_key, tx))
            .await
            .ok()?;
        rx.await.ok()?;
        Some(io)
    }

    fn peer_conn_ref(&self) -> PeerConnRef<'_> {
        self.conn.as_ref()
    }

    pub async fn diagnostics(&self, source_node_id: NodeId) -> PeerConnectionDiagnosticInfo {
        let stats = self.conn.stats().await;
        PeerConnectionDiagnosticInfo {
            source: Some(source_node_id.into()),
            destination: Some(self.node_id().into()),
            is_client: Some(self.endpoint == Endpoint::Client),
            is_established: Some(self.conn.is_established().await),
            received_packets: stats.as_ref().map(|stats| stats.recv as u64),
            sent_packets: stats.as_ref().map(|stats| stats.sent as u64),
            lost_packets: stats.as_ref().map(|stats| stats.lost as u64),
            messages_sent: Some(self.channel_proxy_stats.sent_messages()),
            bytes_sent: Some(self.channel_proxy_stats.sent_bytes()),
            connect_to_service_sends: Some(self.conn_stats.connect_to_service.sent_messages()),
            connect_to_service_send_bytes: Some(self.conn_stats.connect_to_service.sent_bytes()),
            update_node_description_sends: Some(
                self.conn_stats.update_node_description.sent_messages(),
            ),
            update_node_description_send_bytes: Some(
                self.conn_stats.update_node_description.sent_bytes(),
            ),
            update_link_status_sends: Some(self.conn_stats.update_link_status.sent_messages()),
            update_link_status_send_bytes: Some(self.conn_stats.update_link_status.sent_bytes()),
            update_link_status_ack_sends: Some(
                self.conn_stats.update_link_status_ack.sent_messages(),
            ),
            update_link_status_ack_send_bytes: Some(
                self.conn_stats.update_link_status_ack.sent_bytes(),
            ),
            round_trip_time_microseconds: stats
                .as_ref()
                .map(|stats| stats.rtt.as_micros().try_into().unwrap_or(std::u64::MAX)),
            congestion_window_bytes: stats.as_ref().map(|stats| stats.cwnd as u64),
            ..PeerConnectionDiagnosticInfo::EMPTY
        }
    }
}

enum RawConnection {
    Quic(AsyncQuicStreamWriter, AsyncQuicStreamReader),
    Circuit(circuit::stream::Writer, circuit::stream::Reader, circuit::Connection),
}

const QUIC_CONNECTION_TIMEOUT: Duration = Duration::from_secs(60);

async fn client_handshake(
    my_node_id: NodeId,
    peer_node_id: NodeId,
    mut conn: RawConnection,
    conn_stats: Arc<PeerConnStats>,
) -> Result<(FramedStreamWriter, FramedStreamReader), Error> {
    tracing::trace!(
        my_node_id = my_node_id.0,
        clipeer = peer_node_id.0,
        "client connection stream started",
    );
    // Send FIDL header
    tracing::trace!(
        my_node_id = my_node_id.0,
        clipeer = ?peer_node_id,
        "send fidl header"
    );
    match &mut conn {
        RawConnection::Quic(writer, _) => {
            writer
                .send(&mut [0, 0, 0, fidl::encoding::MAGIC_NUMBER_INITIAL], false)
                .on_timeout(QUIC_CONNECTION_TIMEOUT, || {
                    Err(format_err!("timeout initializing quic connection"))
                })
                .await?
        }
        RawConnection::Circuit(writer, _, _) => {
            let msg = [0, 0, 0, fidl::encoding::MAGIC_NUMBER_INITIAL];
            writer.write(msg.len(), |buf| {
                buf[..msg.len()].copy_from_slice(&msg);
                Ok(msg.len())
            })?;
        }
    };
    async move {
        tracing::trace!(my_node_id = my_node_id.0, clipeer = peer_node_id.0, "send config request");
        // Send config request
        let (mut conn_stream_writer, conn_stream_reader_fut) = match conn {
            RawConnection::Quic(writer, mut reader) => {
                (
                    FramedStreamWriter::from_quic(writer, peer_node_id),
                    async move {
                        // Receive FIDL header
                        tracing::trace!(
                            my_node_id = my_node_id.0,
                            clipeer = peer_node_id.0,
                            "read fidl header"
                        );
                        let mut fidl_hdr = [0u8; 4];
                        reader.read_exact(&mut fidl_hdr).await.context("reading FIDL header")?;
                        Result::<_, Error>::Ok(FramedStreamReader::from_quic(reader, peer_node_id))
                    }
                    .boxed(),
                )
            }
            RawConnection::Circuit(writer, reader, conn) => {
                (
                    FramedStreamWriter::from_circuit(writer, 0, conn.clone(), peer_node_id),
                    async move {
                        // Receive FIDL header
                        tracing::trace!(
                            my_node_id = my_node_id.0,
                            clipeer = peer_node_id.0,
                            "read fidl header"
                        );
                        reader.read(4, |_| Ok(((), 4))).await?;
                        Result::<_, Error>::Ok(FramedStreamReader::from_circuit(
                            reader,
                            conn,
                            peer_node_id,
                        ))
                    }
                    .boxed(),
                )
            }
        };
        let coding_context = coding::DEFAULT_CONTEXT;
        conn_stream_writer
            .send(
                FrameType::Data(coding_context),
                &encode_fidl_with_context(coding_context, &mut ConfigRequest::EMPTY.clone())?,
                false,
                &conn_stats.config,
            )
            .await?;
        // Await config response
        tracing::trace!(my_node_id = my_node_id.0, clipeer = peer_node_id.0, "read config");
        let mut conn_stream_reader = conn_stream_reader_fut.await?;
        let _ = Config::from_response(
            if let (FrameType::Data(coding_context), mut bytes, false) =
                conn_stream_reader.next().await?
            {
                decode_fidl_with_context(coding_context, &mut bytes)?
            } else {
                bail!("Failed to read config response")
            },
        );
        tracing::trace!(my_node_id = my_node_id.0, clipeer = peer_node_id.0, "handshake completed");

        Ok((conn_stream_writer, conn_stream_reader))
    }
    .on_timeout(QUIC_CONNECTION_TIMEOUT, || Err(format_err!("timeout performing handshake")))
    .await
}

struct TrackClientConnection {
    router: Weak<Router>,
    node_id: NodeId,
}

impl TrackClientConnection {
    async fn new(router: &Arc<Router>, node_id: NodeId) -> TrackClientConnection {
        router.service_map().add_client_connection(node_id).await;
        TrackClientConnection { router: Arc::downgrade(router), node_id }
    }
}

impl Drop for TrackClientConnection {
    fn drop(&mut self) {
        if let Some(router) = Weak::upgrade(&self.router) {
            let node_id = self.node_id;
            Task::spawn(
                async move { router.service_map().remove_client_connection(node_id).await },
            )
            .detach();
        }
    }
}

async fn client_conn_stream(
    router: Weak<Router>,
    peer_node_id: NodeId,
    conn: RawConnection,
    mut commands: mpsc::Receiver<ClientPeerCommand>,
    mut services: Observer<Vec<String>>,
    conn_stats: Arc<PeerConnStats>,
) -> Result<(), RunnerError> {
    let get_router = move || Weak::upgrade(&router).ok_or_else(|| RunnerError::RouterGone);
    let my_node_id = get_router()?.node_id();

    let (conn_stream_writer, mut conn_stream_reader) =
        client_handshake(my_node_id, peer_node_id, conn, conn_stats.clone())
            .map_err(RunnerError::HandshakeError)
            .await?;

    let _track_connection = TrackClientConnection::new(&get_router()?, peer_node_id).await;

    let conn_stream_writer = &Mutex::new(conn_stream_writer);

    let cmd_conn_stats = conn_stats.clone();
    let svc_conn_stats = conn_stats;

    let _: ((), (), ()) = futures::future::try_join3(
        async move {
            while let Some(command) = commands.next().await {
                tracing::trace!(
                    my_node_id = my_node_id.0,
                    clipeer = peer_node_id.0,
                    "handle command: {:?}",
                    command
                );
                client_conn_handle_command(
                    command,
                    &mut *conn_stream_writer.lock().await,
                    cmd_conn_stats.clone(),
                )
                .await?;
            }
            tracing::trace!(my_node_id = my_node_id.0, clipeer = peer_node_id.0, "done commands");
            Ok(())
        }
        .map_err(RunnerError::ServiceError),
        async move {
            loop {
                let (frame_type, mut bytes, fin) =
                    conn_stream_reader.next().await.map_err(RunnerError::ServiceError)?;
                match frame_type {
                    FrameType::Hello | FrameType::Control(_) | FrameType::Signal(_) => {
                        return Err(RunnerError::BadFrameType(frame_type));
                    }
                    FrameType::Data(coding_context) => {
                        client_conn_handle_incoming_frame(
                            my_node_id,
                            peer_node_id,
                            &mut bytes,
                            coding_context,
                        )
                        .await
                        .map_err(RunnerError::ServiceError)?;
                    }
                }
                if fin {
                    return Err(RunnerError::ConnectionClosed);
                }
            }
        },
        async move {
            loop {
                let services = services.next().await;
                tracing::trace!(
                    my_node_id = my_node_id.0,
                    clipeer = peer_node_id.0,
                    "Send update node description with services: {:?}",
                    services
                );
                let coding_context = coding::DEFAULT_CONTEXT;
                conn_stream_writer
                    .lock()
                    .await
                    .send(
                        FrameType::Data(coding_context),
                        &encode_fidl_with_context(
                            coding_context,
                            &mut PeerMessage::UpdateNodeDescription(PeerDescription {
                                services,
                                ..PeerDescription::EMPTY
                            }),
                        )?,
                        false,
                        &svc_conn_stats.update_node_description,
                    )
                    .await?;
            }
        }
        .map_err(RunnerError::ServiceError),
    )
    .await?;

    Ok(())
}

async fn client_conn_handle_command(
    command: ClientPeerCommand,
    conn_stream_writer: &mut FramedStreamWriter,
    conn_stats: Arc<PeerConnStats>,
) -> Result<(), Error> {
    match command {
        ClientPeerCommand::ConnectToService(conn) => {
            let coding_context = coding::DEFAULT_CONTEXT;
            conn_stream_writer
                .send(
                    FrameType::Data(coding_context),
                    &encode_fidl_with_context(
                        coding_context,
                        &mut PeerMessage::ConnectToService(conn),
                    )?,
                    false,
                    &conn_stats.connect_to_service,
                )
                .await?;
        }
        ClientPeerCommand::OpenTransfer(stream_id, transfer_key, sent) => {
            let coding_context = coding::DEFAULT_CONTEXT;
            conn_stream_writer
                .send(
                    FrameType::Data(coding_context),
                    &encode_fidl_with_context(
                        coding_context,
                        &mut PeerMessage::OpenTransfer(OpenTransfer {
                            stream_id: StreamId { id: stream_id },
                            transfer_key,
                        }),
                    )?,
                    false,
                    &conn_stats.open_transfer,
                )
                .await?;
            let _ = sent.send(());
        }
    }
    Ok(())
}

async fn client_conn_handle_incoming_frame(
    my_node_id: NodeId,
    peer_node_id: NodeId,
    bytes: &mut [u8],
    coding_context: coding::Context,
) -> Result<(), Error> {
    let msg: PeerReply = decode_fidl_with_context(coding_context, bytes)?;
    tracing::trace!(my_node_id = my_node_id.0, clipeer = peer_node_id.0, "got reply {:?}", msg);
    match msg {
        PeerReply::UpdateLinkStatusAck(_) => {
            // XXX(raggi): prior code attempted to send to a None under a lock
            // here, seemingly unused, but may have influenced total ordering?
        }
    }
    Ok(())
}

async fn server_handshake(
    my_node_id: NodeId,
    node_id: NodeId,
    conn: RawConnection,
    conn_stats: Arc<PeerConnStats>,
) -> Result<(FramedStreamWriter, FramedStreamReader), Error> {
    // Receive FIDL header
    tracing::trace!(my_node_id = my_node_id.0, svrpeer = node_id.0, "read fidl header");
    let (mut conn_stream_reader, mut conn_stream_writer) = match conn {
        RawConnection::Quic(mut writer, mut reader) => {
            let mut fidl_hdr = [0u8; 4];
            reader.read_exact(&mut fidl_hdr).await.context("reading FIDL header")?;
            // Send FIDL header
            tracing::trace!(
                my_node_id = my_node_id.0,
                svrpeer = ?node_id,
                "send fidl header"
            );
            writer.send(&mut [0, 0, 0, fidl::encoding::MAGIC_NUMBER_INITIAL], false).await?;
            (
                FramedStreamReader::from_quic(reader, node_id),
                FramedStreamWriter::from_quic(writer, node_id),
            )
        }
        RawConnection::Circuit(writer, reader, conn) => {
            reader.read(4, |_| Ok(((), 4))).await.context("reading FIDL header")?;
            // Send FIDL header
            tracing::trace!(
                my_node_id = my_node_id.0,
                svrpeer = ?node_id,
                "send fidl header"
            );
            let handshake = [0, 0, 0, fidl::encoding::MAGIC_NUMBER_INITIAL];
            writer.write(handshake.len(), |buf| {
                buf[..handshake.len()].copy_from_slice(&handshake);
                Ok(handshake.len())
            })?;
            (
                FramedStreamReader::from_circuit(reader, conn.clone(), node_id),
                FramedStreamWriter::from_circuit(writer, 0, conn.clone(), node_id),
            )
        }
    };
    // Await config request
    tracing::trace!(my_node_id = my_node_id.0, svrpeer = node_id.0, "read config");
    let (_, mut response) = Config::negotiate(
        if let (FrameType::Data(coding_context), mut bytes, false) =
            conn_stream_reader.next().await?
        {
            decode_fidl_with_context(coding_context, &mut bytes)?
        } else {
            bail!("Failed to read config response")
        },
    );
    // Send config response
    tracing::trace!(my_node_id = my_node_id.0, svrpeer = node_id.0, "send config");
    let coding_context = coding::Context { use_persistent_header: false };
    conn_stream_writer
        .send(
            FrameType::Data(coding_context),
            &encode_fidl_with_context(coding_context, &mut response)?,
            false,
            &conn_stats.config,
        )
        .await?;
    Ok((conn_stream_writer, conn_stream_reader))
}

async fn server_conn_stream(
    node_id: NodeId,
    conn: RawConnection,
    router: Weak<Router>,
    conn_stats: Arc<PeerConnStats>,
    channel_proxy_stats: Arc<MessageStats>,
) -> Result<(), RunnerError> {
    let my_node_id = Weak::upgrade(&router).ok_or_else(|| RunnerError::RouterGone)?.node_id();
    let (conn_stream_writer, mut conn_stream_reader) =
        server_handshake(my_node_id, node_id, conn, conn_stats)
            .map_err(RunnerError::HandshakeError)
            .await?;

    loop {
        tracing::trace!(my_node_id = my_node_id.0, svrpeer = node_id.0, "await message");
        let (frame_type, mut bytes, fin) =
            conn_stream_reader.next().map_err(RunnerError::ServiceError).await?;

        let router = Weak::upgrade(&router).ok_or_else(|| RunnerError::RouterGone)?;
        match frame_type {
            FrameType::Hello | FrameType::Control(_) | FrameType::Signal(_) => {
                return Err(RunnerError::BadFrameType(frame_type));
            }
            FrameType::Data(coding_context) => {
                let msg: PeerMessage = decode_fidl_with_context(coding_context, &mut bytes)
                    .map_err(RunnerError::ServiceError)?;
                tracing::trace!(
                    my_node_id = my_node_id.0,
                    svrpeer = node_id.0,
                    "Got peer request: {:?}",
                    msg
                );
                match msg {
                    PeerMessage::ConnectToService(ConnectToService {
                        service_name,
                        stream_ref,
                        rights,
                        options: _,
                    }) => {
                        let app_channel = Channel::from_handle(
                            router
                                .recv_proxied(
                                    ZirconHandle::Channel(ChannelHandle { stream_ref, rights }),
                                    conn_stream_writer.conn(),
                                    channel_proxy_stats.clone(),
                                )
                                .map_err(RunnerError::ServiceError)
                                .await?,
                        );
                        router
                            .service_map()
                            .connect(
                                &service_name,
                                app_channel,
                                ConnectionInfo {
                                    peer: Some(node_id.into()),
                                    ..ConnectionInfo::EMPTY
                                },
                            )
                            .map_err(RunnerError::ServiceError)
                            .await?;
                    }
                    PeerMessage::UpdateNodeDescription(PeerDescription { services, .. }) => {
                        router
                            .service_map()
                            .update_node(node_id, services.unwrap_or(vec![]))
                            .map_err(RunnerError::ServiceError)
                            .await?;
                    }
                    PeerMessage::OpenTransfer(OpenTransfer {
                        stream_id: StreamId { id: stream_id },
                        transfer_key,
                    }) => {
                        let (tx, rx) = conn_stream_writer
                            .conn()
                            .bind_id(stream_id)
                            .await
                            .map_err(RunnerError::ServiceError)?;
                        router
                            .post_transfer(transfer_key, FoundTransfer::Remote(tx, rx))
                            .map_err(RunnerError::ServiceError)
                            .await?;
                    }
                }
            }
        }

        if fin {
            return Err(RunnerError::ConnectionClosed);
        }
    }
}
