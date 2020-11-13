// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    async_quic::{AsyncConnection, AsyncQuicStreamReader, AsyncQuicStreamWriter, StreamProperties},
    coding::{decode_fidl, encode_fidl},
    framed_stream::{FrameType, FramedStreamReader, FramedStreamWriter, MessageStats},
    future_help::Observer,
    labels::{ConnectionId, Endpoint, NodeId, TransferKey},
    link::LinkStatus,
    route_planner::RemoteRoutingUpdate,
    router::{FoundTransfer, LinkSource, Router},
};
use anyhow::{bail, format_err, Context as _, Error};
use fidl::{Channel, HandleBased};
use fidl_fuchsia_overnet::ConnectionInfo;
use fidl_fuchsia_overnet_protocol::{
    ChannelHandle, ConfigRequest, ConfigResponse, ConnectToService, ConnectToServiceOptions,
    OpenTransfer, PeerConnectionDiagnosticInfo, PeerDescription, PeerMessage, PeerReply, StreamId,
    UpdateLinkStatus, ZirconHandle,
};
use fuchsia_async::{Task, TimeoutExt, Timer};
use futures::{
    channel::{mpsc, oneshot},
    lock::Mutex,
    prelude::*,
};
use std::{
    collections::HashMap,
    convert::TryInto,
    pin::Pin,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Weak,
    },
    time::Duration,
};

#[derive(Debug)]
struct Config {}

impl Config {
    fn negotiate(_request: ConfigRequest) -> (Self, ConfigResponse) {
        (Config {}, ConfigResponse::empty())
    }

    fn from_response(_response: ConfigResponse) -> Self {
        Config {}
    }
}

#[derive(Debug)]
enum ClientPeerCommand {
    ConnectToService(ConnectToService),
    OpenTransfer(u64, TransferKey, oneshot::Sender<()>),
    Ping(oneshot::Sender<()>),
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

pub(crate) struct Peer {
    node_id: NodeId,
    endpoint: Endpoint,
    conn_id: ConnectionId,
    /// The QUIC connection itself
    conn: AsyncConnection,
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

impl Drop for Peer {
    fn drop(&mut self) {
        let shutdown = self.shutdown.load(Ordering::Acquire);
        if !shutdown {
            let conn = self.conn.clone();
            Task::spawn(async move { conn.close().await }).detach()
        }
    }
}

impl Peer {
    pub(crate) fn node_id(&self) -> NodeId {
        self.node_id
    }

    pub(crate) fn endpoint(&self) -> Endpoint {
        self.endpoint
    }

    pub(crate) fn conn_id(&self) -> ConnectionId {
        self.conn_id
    }

    pub(crate) fn debug_id(&self) -> impl std::fmt::Debug + std::cmp::PartialEq {
        (self.node_id, self.endpoint, self.conn_id)
    }

    pub(crate) async fn round_trip_time(&self) -> Duration {
        self.conn.stats().await.rtt
    }

    /// Construct a new client peer - spawns tasks to handle making control stream requests, and
    /// publishing link metadata
    pub fn new_client(
        node_id: NodeId,
        conn_id: ConnectionId,
        conn: Pin<Box<quiche::Connection>>,
        link_status_observer: Observer<Vec<LinkStatus>>,
        service_observer: Observer<Vec<String>>,
        router: &Arc<Router>,
        but_why: &str,
    ) -> Arc<Self> {
        log::trace!(
            "[{:?}] NEW CLIENT: peer={:?} conn_id={:?} because {:?}",
            router.node_id(),
            node_id,
            conn_id,
            but_why
        );
        let (command_sender, command_receiver) = mpsc::channel(1);
        let conn =
            AsyncConnection::from_connection(conn, Endpoint::Client, router.node_id(), node_id);
        let conn_stats = Arc::new(PeerConnStats::default());
        let (conn_stream_writer, conn_stream_reader) = conn.bind_id(0);
        let conn_run = conn.run();
        Arc::new(Self {
            endpoint: Endpoint::Client,
            node_id,
            conn_id,
            conn,
            commands: Some(command_sender.clone()),
            conn_stats: conn_stats.clone(),
            channel_proxy_stats: Arc::new(MessageStats::default()),
            shutdown: AtomicBool::new(false),
            _task: Task::spawn(Peer::runner(
                Endpoint::Client,
                Arc::downgrade(router),
                conn_id,
                futures::future::try_join(
                    client_conn_stream(
                        Arc::downgrade(router),
                        node_id,
                        conn_stream_writer,
                        conn_stream_reader,
                        command_receiver,
                        link_status_observer,
                        service_observer,
                        conn_stats,
                    ),
                    conn_run,
                )
                .map_ok(drop),
            )),
        })
    }

    /// Construct a new server peer - spawns tasks to handle responding to control stream requests
    pub fn new_server(
        node_id: NodeId,
        conn_id: ConnectionId,
        conn: Pin<Box<quiche::Connection>>,
        router: &Arc<Router>,
    ) -> Arc<Self> {
        let router = &router;
        log::trace!(
            "[{:?}] NEW SERVER: peer={:?} conn_id={:?}",
            router.node_id(),
            node_id,
            conn_id,
        );
        let conn =
            AsyncConnection::from_connection(conn, Endpoint::Server, router.node_id(), node_id);
        let conn_stats = Arc::new(PeerConnStats::default());
        let (conn_stream_writer, conn_stream_reader) = conn.bind_id(0);
        let conn_run = conn.run();
        let channel_proxy_stats = Arc::new(MessageStats::default());
        Arc::new(Self {
            endpoint: Endpoint::Server,
            node_id,
            conn_id,
            conn,
            commands: None,
            conn_stats: conn_stats.clone(),
            channel_proxy_stats: channel_proxy_stats.clone(),
            shutdown: AtomicBool::new(false),
            _task: Task::spawn(Peer::runner(
                Endpoint::Server,
                Arc::downgrade(router),
                conn_id,
                futures::future::try_join(
                    server_conn_stream(
                        node_id,
                        conn_stream_writer,
                        conn_stream_reader,
                        Arc::downgrade(router),
                        conn_stats,
                        channel_proxy_stats,
                    ),
                    conn_run,
                )
                .map_ok(drop),
            )),
        })
    }

    async fn runner(
        endpoint: Endpoint,
        router: Weak<Router>,
        conn_id: ConnectionId,
        f: impl Future<Output = Result<(), Error>>,
    ) {
        if let Err(e) = f.await {
            let node_id = Weak::upgrade(&router)
                .map(|r| format!("{:?}", r.node_id()))
                .unwrap_or_else(String::new);
            log::warn!("[{} conn:{:?}] {:?} runner error: {:?}", node_id, conn_id, endpoint, e);
        }
        if let Some(router) = Weak::upgrade(&router) {
            router.remove_peer(conn_id).await;
        }
    }

    pub async fn shutdown(&self) {
        self.shutdown.store(true, Ordering::Release);
        self.conn.close().await
    }

    pub async fn ping(&self) -> Result<(), Error> {
        let (tx, rx) = oneshot::channel();
        self.commands.as_ref().unwrap().clone().send(ClientPeerCommand::Ping(tx)).await?;
        rx.await?;
        Ok(())
    }

    pub fn next_send<'a>(&'a self) -> crate::async_quic::NextSend<'a> {
        self.conn.next_send()
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
            .clone()
            .send_proxied(chan.into_handle(), self.conn.clone(), self.channel_proxy_stats.clone())
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
                    options: ConnectToServiceOptions::empty(),
                }))
                .await?;
            Ok(())
        } else {
            Err(format_err!("Not a real channel trying to connect to service"))
        }
    }

    pub async fn send_open_transfer(
        &self,
        transfer_key: TransferKey,
    ) -> Option<(AsyncQuicStreamWriter, AsyncQuicStreamReader)> {
        let io = self.conn.alloc_bidi();
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

    pub async fn diagnostics(&self, source_node_id: NodeId) -> PeerConnectionDiagnosticInfo {
        let stats = self.conn.stats().await;
        PeerConnectionDiagnosticInfo {
            source: Some(source_node_id.into()),
            destination: Some(self.node_id.into()),
            is_client: Some(self.endpoint == Endpoint::Client),
            is_established: Some(self.conn.is_established().await),
            received_packets: Some(stats.recv as u64),
            sent_packets: Some(stats.sent as u64),
            lost_packets: Some(stats.lost as u64),
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
            round_trip_time_microseconds: Some(
                stats.rtt.as_micros().try_into().unwrap_or(std::u64::MAX),
            ),
            congestion_window_bytes: Some(stats.cwnd as u64),
            ..PeerConnectionDiagnosticInfo::empty()
        }
    }
}

async fn client_handshake(
    my_node_id: NodeId,
    peer_node_id: NodeId,
    mut conn_stream_writer: AsyncQuicStreamWriter,
    mut conn_stream_reader: AsyncQuicStreamReader,
    conn_stats: Arc<PeerConnStats>,
) -> Result<(FramedStreamWriter, FramedStreamReader), Error> {
    log::trace!("[{:?} clipeer:{:?}] client connection stream started", my_node_id, peer_node_id);
    // Send FIDL header
    log::trace!("[{:?} clipeer:{:?}] send fidl header", my_node_id, peer_node_id);
    conn_stream_writer
        .send(&mut [0, 0, 0, fidl::encoding::MAGIC_NUMBER_INITIAL], false)
        .on_timeout(Duration::from_secs(60), || Err(format_err!("timeout")))
        .await?;
    async move {
        log::trace!("[{:?} clipeer:{:?}] send config request", my_node_id, peer_node_id);
        // Send config request
        let mut conn_stream_writer: FramedStreamWriter = conn_stream_writer.into();
        conn_stream_writer
            .send(
                FrameType::Data,
                &encode_fidl(&mut ConfigRequest::empty())?,
                false,
                &conn_stats.config,
            )
            .await?;
        // Receive FIDL header
        log::trace!("[{:?} clipeer:{:?}] read fidl header", my_node_id, peer_node_id);
        let mut fidl_hdr = [0u8; 4];
        conn_stream_reader.read_exact(&mut fidl_hdr).await.context("reading FIDL header")?;
        // Await config response
        log::trace!("[{:?} clipeer:{:?}] read config", my_node_id, peer_node_id);
        let mut conn_stream_reader: FramedStreamReader = conn_stream_reader.into();
        let _ = Config::from_response(
            if let (FrameType::Data, mut bytes, false) = conn_stream_reader.next().await? {
                decode_fidl(&mut bytes)?
            } else {
                bail!("Failed to read config response")
            },
        );
        log::trace!("[{:?} clipeer:{:?}] handshake completed", my_node_id, peer_node_id);

        Ok((conn_stream_writer, conn_stream_reader))
    }
    .on_timeout(Duration::from_secs(20), || Err(format_err!("timeout")))
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
    conn_stream_writer: AsyncQuicStreamWriter,
    conn_stream_reader: AsyncQuicStreamReader,
    mut commands: mpsc::Receiver<ClientPeerCommand>,
    mut link_status_observer: Observer<Vec<LinkStatus>>,
    mut services: Observer<Vec<String>>,
    conn_stats: Arc<PeerConnStats>,
) -> Result<(), Error> {
    let get_router = move || Weak::upgrade(&router).ok_or_else(|| format_err!("router gone"));
    let my_node_id = get_router()?.node_id();

    let (conn_stream_writer, mut conn_stream_reader) = client_handshake(
        my_node_id,
        peer_node_id,
        conn_stream_writer,
        conn_stream_reader,
        conn_stats.clone(),
    )
    .await?;

    let _track_connection = TrackClientConnection::new(&get_router()?, peer_node_id).await;

    let on_link_status_ack = &Mutex::new(None);
    let conn_stream_writer = &Mutex::new(conn_stream_writer);
    let outstanding_pings = &Mutex::new(HashMap::new());
    let mut next_ping_id = 0;

    let cmd_conn_stats = conn_stats.clone();
    let svc_conn_stats = conn_stats.clone();
    let sts_conn_stats = conn_stats;

    let _: ((), (), (), ()) = futures::future::try_join4(
        async move {
            while let Some(command) = commands.next().await {
                log::trace!(
                    "[{:?} clipeer:{:?}] handle command: {:?}",
                    my_node_id,
                    peer_node_id,
                    command
                );
                client_conn_handle_command(
                    command,
                    &mut *conn_stream_writer.lock().await,
                    outstanding_pings,
                    &mut next_ping_id,
                    cmd_conn_stats.clone(),
                )
                .await?;
            }
            log::trace!("[{:?} clipeer:{:?}] done commands", my_node_id, peer_node_id);
            Ok(())
        },
        async move {
            loop {
                let (frame_type, mut bytes, fin) = conn_stream_reader.next().await?;
                match frame_type {
                    FrameType::Hello => bail!("Hello frames disallowed on peer connections"),
                    FrameType::Control => bail!("Control frames disallowed on peer connections"),
                    FrameType::Data => {
                        client_conn_handle_incoming_frame(
                            my_node_id,
                            peer_node_id,
                            &mut bytes,
                            on_link_status_ack,
                            outstanding_pings,
                        )
                        .await?;
                    }
                }
                if fin {
                    bail!("Client connection stream closed");
                }
            }
        },
        async move {
            loop {
                let services = services.next().await;
                log::trace!(
                    "[{:?} clipeer:{:?}] Send update node description with services: {:?}",
                    my_node_id,
                    peer_node_id,
                    services
                );
                conn_stream_writer
                    .lock()
                    .await
                    .send(
                        FrameType::Data,
                        &encode_fidl(&mut PeerMessage::UpdateNodeDescription(PeerDescription {
                            services,
                            ..PeerDescription::empty()
                        }))?,
                        false,
                        &svc_conn_stats.update_node_description,
                    )
                    .await?;
            }
        },
        async move {
            log::trace!("[{:?} clipeer:{:?}] SEND_LINKS BEGINS", my_node_id, peer_node_id);
            while let Some(link_status) = link_status_observer.next().await {
                log::trace!(
                    "[{:?} clipeer:{:?}] SEND_LINKS SCHEDULE SEND {:?}",
                    my_node_id,
                    peer_node_id,
                    link_status
                );
                let (sender, receiver) = oneshot::channel();
                {
                    let mut on_link_status_ack = on_link_status_ack.lock().await;
                    assert!(on_link_status_ack.is_none());
                    *on_link_status_ack = Some(sender);
                }
                conn_stream_writer
                    .lock()
                    .await
                    .send(
                        FrameType::Data,
                        &encode_fidl(&mut PeerMessage::UpdateLinkStatus(UpdateLinkStatus {
                            link_status: link_status.into_iter().map(|s| s.into()).collect(),
                        }))?,
                        false,
                        &sts_conn_stats.update_link_status,
                    )
                    .await?;
                log::trace!("[{:?} clipeer:{:?}] SEND_LINKS AWAIT ACK", my_node_id, peer_node_id);
                receiver.await?;

                log::trace!("[{:?} clipeer:{:?}] SEND_LINKS SLEEPY TIME", my_node_id, peer_node_id);
                Timer::new(Duration::from_secs(1)).await;
            }
            log::trace!("[{:?} clipeer:{:?}] SEND_LINKS END", my_node_id, peer_node_id);
            Ok(())
        },
    )
    .await?;

    Ok(())
}

async fn client_conn_handle_command(
    command: ClientPeerCommand,
    conn_stream_writer: &mut FramedStreamWriter,
    outstanding_pings: &Mutex<HashMap<u64, oneshot::Sender<()>>>,
    next_ping_id: &mut u64,
    conn_stats: Arc<PeerConnStats>,
) -> Result<(), Error> {
    match command {
        ClientPeerCommand::ConnectToService(conn) => {
            conn_stream_writer
                .send(
                    FrameType::Data,
                    &encode_fidl(&mut PeerMessage::ConnectToService(conn))?,
                    false,
                    &conn_stats.connect_to_service,
                )
                .await?;
        }
        ClientPeerCommand::OpenTransfer(stream_id, transfer_key, sent) => {
            conn_stream_writer
                .send(
                    FrameType::Data,
                    &encode_fidl(&mut PeerMessage::OpenTransfer(OpenTransfer {
                        stream_id: StreamId { id: stream_id },
                        transfer_key,
                    }))?,
                    false,
                    &conn_stats.open_transfer,
                )
                .await?;
            let _ = sent.send(());
        }
        ClientPeerCommand::Ping(on_ack) => {
            let ping_id = *next_ping_id;
            *next_ping_id += 1;
            outstanding_pings.lock().await.insert(ping_id, on_ack);
            conn_stream_writer
                .send(
                    FrameType::Data,
                    &encode_fidl(&mut PeerMessage::Ping(ping_id))?,
                    false,
                    &conn_stats.ping,
                )
                .await?;
        }
    }
    Ok(())
}

async fn client_conn_handle_incoming_frame(
    my_node_id: NodeId,
    peer_node_id: NodeId,
    bytes: &mut [u8],
    on_link_status_ack: &Mutex<Option<oneshot::Sender<()>>>,
    outstanding_pings: &Mutex<HashMap<u64, oneshot::Sender<()>>>,
) -> Result<(), Error> {
    let msg: PeerReply = decode_fidl(bytes)?;
    log::trace!("[{:?} clipeer:{:?}] got reply {:?}", my_node_id, peer_node_id, msg);
    match msg {
        PeerReply::UpdateLinkStatusAck(_) => {
            on_link_status_ack
                .lock()
                .await
                .take()
                .ok_or_else(|| format_err!("Got link status ack without sending link status"))?
                .send(())
                .map_err(|_| format_err!("Failed to send link status ack"))?;
        }
        PeerReply::Pong(id) => {
            let _ = outstanding_pings
                .lock()
                .await
                .remove(&id)
                .ok_or_else(|| format_err!("Pong for unsent ping {:?}", id))?
                .send(());
        }
    }
    Ok(())
}

async fn server_conn_stream(
    node_id: NodeId,
    mut conn_stream_writer: AsyncQuicStreamWriter,
    mut conn_stream_reader: AsyncQuicStreamReader,
    router: Weak<Router>,
    conn_stats: Arc<PeerConnStats>,
    channel_proxy_stats: Arc<MessageStats>,
) -> Result<(), Error> {
    let my_node_id = Weak::upgrade(&router).ok_or_else(|| format_err!("router gone"))?.node_id();
    // Receive FIDL header
    log::trace!("[{:?} svrpeer:{:?}] read fidl header", my_node_id, node_id);
    let mut fidl_hdr = [0u8; 4];
    conn_stream_reader.read_exact(&mut fidl_hdr).await.context("reading FIDL header")?;
    let mut conn_stream_reader: FramedStreamReader = conn_stream_reader.into();
    // Send FIDL header
    log::trace!("[{:?} svrpeer:{:?}] send fidl header", my_node_id, node_id);
    conn_stream_writer.send(&mut [0, 0, 0, fidl::encoding::MAGIC_NUMBER_INITIAL], false).await?;
    let mut conn_stream_writer: FramedStreamWriter = conn_stream_writer.into();
    // Await config request
    log::trace!("[{:?} svrpeer:{:?}] read config", my_node_id, node_id);
    let (_, mut response) = Config::negotiate(
        if let (FrameType::Data, mut bytes, false) = conn_stream_reader.next().await? {
            decode_fidl(&mut bytes)?
        } else {
            bail!("Failed to read config response")
        },
    );
    // Send config response
    log::trace!("[{:?} svrpeer:{:?}] send config", my_node_id, node_id);
    conn_stream_writer
        .send(FrameType::Data, &encode_fidl(&mut response)?, false, &conn_stats.config)
        .await?;

    loop {
        log::trace!("[{:?} svrpeer:{:?}] await message", my_node_id, node_id);
        let (frame_type, mut bytes, fin) = conn_stream_reader.next().await?;

        let router = Weak::upgrade(&router)
            .ok_or_else(|| format_err!("Router gone during connection stream message handling"))?;
        match frame_type {
            FrameType::Hello => bail!("Hello frames disallowed on peer connections"),
            FrameType::Control => bail!("Control frames disallowed on peer connections"),
            FrameType::Data => {
                let msg: PeerMessage = decode_fidl(&mut bytes)?;
                log::trace!("[{:?} svrpeer:{:?}] Got peer request: {:?}", my_node_id, node_id, msg);
                match msg {
                    PeerMessage::ConnectToService(ConnectToService {
                        service_name,
                        stream_ref,
                        rights,
                        options: _,
                    }) => {
                        let app_channel = Channel::from_handle(
                            router
                                .clone()
                                .recv_proxied(
                                    ZirconHandle::Channel(ChannelHandle { stream_ref, rights }),
                                    conn_stream_writer.conn().clone(),
                                    channel_proxy_stats.clone(),
                                )
                                .await?,
                        );
                        router
                            .service_map()
                            .connect(
                                &service_name,
                                app_channel,
                                ConnectionInfo {
                                    peer: Some(node_id.into()),
                                    ..ConnectionInfo::empty()
                                },
                            )
                            .await?;
                    }
                    PeerMessage::UpdateNodeDescription(PeerDescription { services, .. }) => {
                        router
                            .service_map()
                            .update_node(node_id, services.unwrap_or(vec![]))
                            .await?;
                    }
                    PeerMessage::UpdateLinkStatus(UpdateLinkStatus { link_status }) => {
                        conn_stream_writer
                            .send(
                                FrameType::Data,
                                &encode_fidl(&mut PeerReply::UpdateLinkStatusAck(
                                    fidl_fuchsia_overnet_protocol::Empty {},
                                ))?,
                                false,
                                &conn_stats.update_link_status_ack,
                            )
                            .await?;
                        let mut status = Vec::new();
                        for fidl_fuchsia_overnet_protocol::LinkStatus {
                            to,
                            local_id,
                            metrics,
                            ..
                        } in link_status
                        {
                            let to: NodeId = to.id.into();
                            router
                                .ensure_client_peer(
                                    to,
                                    (node_id, LinkSource::PeerReachable),
                                    &format!("link from {:?} to {:?}", node_id, to),
                                )
                                .await
                                .context("Summoning due to received metrics")?;
                            status.push(LinkStatus {
                                to,
                                local_id: local_id.into(),
                                round_trip_time: std::time::Duration::from_micros(
                                    metrics.round_trip_time.unwrap_or(std::u64::MAX),
                                ),
                            });
                        }
                        router
                            .send_routing_update(RemoteRoutingUpdate {
                                from_node_id: node_id,
                                status,
                            })
                            .await;
                    }
                    PeerMessage::OpenTransfer(OpenTransfer {
                        stream_id: StreamId { id: stream_id },
                        transfer_key,
                    }) => {
                        let (tx, rx) = conn_stream_writer.conn().bind_id(stream_id);
                        router.post_transfer(transfer_key, FoundTransfer::Remote(tx, rx)).await?;
                    }
                    PeerMessage::Ping(id) => {
                        conn_stream_writer
                            .send(
                                FrameType::Data,
                                &encode_fidl(&mut PeerReply::Pong(id))?,
                                false,
                                &conn_stats.pong,
                            )
                            .await?;
                    }
                }
            }
        }

        if fin {
            bail!("Server connection stream closed");
        }
    }
}
