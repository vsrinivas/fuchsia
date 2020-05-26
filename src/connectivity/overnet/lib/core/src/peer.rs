// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    async_quic::{AsyncConnection, AsyncQuicStreamReader, AsyncQuicStreamWriter, StreamProperties},
    coding::{decode_fidl, encode_fidl},
    framed_stream::{FrameType, FramedStreamReader, FramedStreamWriter, MessageStats},
    future_help::{log_errors, Observer},
    labels::{Endpoint, NodeId, TransferKey},
    link::{Link, LinkStatus},
    route_planner::RemoteRoutingUpdate,
    router::{FoundTransfer, Router},
    runtime::{wait_until, Task},
    Trace,
};
use anyhow::{bail, format_err, Context as _, Error};
use fidl::{Channel, HandleBased};
use fidl_fuchsia_overnet::ConnectionInfo;
use fidl_fuchsia_overnet_protocol::{
    ChannelHandle, ConfigRequest, ConfigResponse, ConnectToService, ConnectToServiceOptions,
    OpenTransfer, PeerConnectionDiagnosticInfo, PeerDescription, PeerMessage, PeerReply, StreamId,
    UpdateLinkStatus, ZirconHandle,
};
use futures::{lock::Mutex, prelude::*};
use std::{
    convert::TryInto,
    pin::Pin,
    sync::{Arc, Weak},
};

#[derive(Debug)]
struct Config {}

impl Config {
    fn negotiate(_request: ConfigRequest) -> (Self, ConfigResponse) {
        (Config {}, ConfigResponse {})
    }

    fn from_response(_response: ConfigResponse) -> Self {
        Config {}
    }
}

#[derive(Debug)]
enum ClientPeerCommand {
    UpdateLinkStatus(UpdateLinkStatus, futures::channel::oneshot::Sender<()>),
    ConnectToService(ConnectToService),
    OpenTransfer(u64, TransferKey),
}

#[derive(Default)]
pub struct PeerConnStats {
    pub config: MessageStats,
    pub connect_to_service: MessageStats,
    pub update_node_description: MessageStats,
    pub update_link_status: MessageStats,
    pub update_link_status_ack: MessageStats,
    pub open_transfer: MessageStats,
}

pub(crate) struct Peer {
    node_id: NodeId,
    endpoint: Endpoint,
    /// The QUIC connection itself
    conn: AsyncConnection,
    /// Current link choice for this peer
    current_link: Mutex<Weak<Link>>,
    commands: Option<futures::channel::mpsc::Sender<ClientPeerCommand>>,
    conn_stats: Arc<PeerConnStats>,
    channel_proxy_stats: Arc<MessageStats>,
    task: Mutex<Option<Task>>,
}

impl std::fmt::Debug for Peer {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.id().fmt(f)
    }
}

impl Peer {
    pub(crate) fn node_id(&self) -> NodeId {
        self.node_id
    }

    pub(crate) fn endpoint(&self) -> Endpoint {
        self.endpoint
    }

    pub(crate) fn id(&self) -> (NodeId, Endpoint) {
        (self.node_id, self.endpoint)
    }

    // Common parts of new_client, new_server - create the peer object, get it routed
    async fn new_instance(
        own_node_id: NodeId,
        node_id: NodeId,
        conn: Pin<Box<quiche::Connection>>,
        endpoint: Endpoint,
        current_link: &Weak<Link>,
        commands: Option<futures::channel::mpsc::Sender<ClientPeerCommand>>,
    ) -> Result<Arc<Self>, Error> {
        log::trace!(
            "{:?} CREATE PEER FOR {:?}/{:?} CURRENT_LINK: {:?}",
            own_node_id,
            node_id,
            endpoint,
            Weak::upgrade(&current_link)
        );
        let conn = AsyncConnection::from_connection(conn, endpoint, node_id);
        let p = Arc::new(Self {
            endpoint,
            node_id,
            conn,
            current_link: Mutex::new(current_link.clone()),
            commands,
            conn_stats: Arc::new(PeerConnStats::default()),
            channel_proxy_stats: Arc::new(MessageStats::default()),
            task: Mutex::new(None),
        });
        if let Some(link) = Weak::upgrade(current_link) {
            link.add_route_for_peer(&p).await;
        }
        Ok(p)
    }

    /// Construct a new client peer - spawns tasks to handle making control stream requests, and
    /// publishing link metadata
    pub async fn new_client(
        own_node_id: NodeId,
        node_id: NodeId,
        conn: Pin<Box<quiche::Connection>>,
        current_link: &Weak<Link>,
        link_status_observer: Observer<Vec<LinkStatus>>,
        service_observer: Observer<Vec<String>>,
    ) -> Result<Arc<Self>, Error> {
        log::info!("NEW CLIENT: on={:?} peer={:?}", own_node_id, node_id);
        let (command_sender, command_receiver) = futures::channel::mpsc::channel(1);
        let p = Self::new_instance(
            own_node_id,
            node_id,
            conn,
            Endpoint::Client,
            current_link,
            Some(command_sender.clone()),
        )
        .await?;
        let (conn_stream_writer, conn_stream_reader) = p.conn.bind_id(0);
        let conn_stats = p.conn_stats.clone();
        let conn_run = p.conn.run();
        *p.task.lock().await = Some(Task::spawn(log_errors(
            futures::future::try_join3(
                client_conn_stream(
                    node_id,
                    conn_stream_writer,
                    conn_stream_reader,
                    command_receiver,
                    service_observer,
                    conn_stats,
                ),
                send_links(node_id, command_sender, link_status_observer),
                conn_run,
            )
            .map_ok(drop),
            "Client connection failed",
        )));
        Ok(p)
    }

    /// Construct a new server peer - spawns tasks to handle responding to control stream requests
    pub async fn new_server(
        node_id: NodeId,
        conn: Pin<Box<quiche::Connection>>,
        current_link: &Weak<Link>,
        router: &Arc<Router>,
    ) -> Result<Arc<Self>, Error> {
        log::info!("NEW SERVER: on={:?} peer={:?}", router.node_id(), node_id);
        let p = Self::new_instance(
            router.node_id(),
            node_id,
            conn,
            Endpoint::Server,
            current_link,
            None,
        )
        .await?;
        let (conn_stream_writer, conn_stream_reader) = p.conn.bind_id(0);
        let conn_run = p.conn.run();
        *p.task.lock().await = Some(Task::spawn(log_errors(
            futures::future::try_join(
                server_conn_stream(
                    node_id,
                    conn_stream_writer,
                    conn_stream_reader,
                    Arc::downgrade(router),
                    Arc::downgrade(&p),
                    p.conn_stats.clone(),
                    p.channel_proxy_stats.clone(),
                ),
                conn_run,
            )
            .map_ok(drop),
            "Server connection stream failed",
        )));
        Ok(p)
    }

    pub async fn update_link(self: &Arc<Self>, link: Weak<Link>) {
        let mut current_link = self.current_link.lock().await;
        if let Some(link) = Weak::upgrade(&*current_link) {
            link.remove_route_for_peer(self.id()).await;
        }
        *current_link = link;
        if let Some(link) = Weak::upgrade(&*current_link) {
            link.add_route_for_peer(self).await;
        }
    }

    pub async fn update_link_if_unset(self: &Arc<Self>, link: &Weak<Link>) {
        let mut current_link = self.current_link.lock().await;
        if Weak::upgrade(&*current_link).is_some() {
            return;
        }
        *current_link = link.clone();
        if let Some(link) = Weak::upgrade(&*current_link) {
            link.add_route_for_peer(self).await;
        }
    }

    pub fn next_send<'a>(&'a self) -> crate::async_quic::NextSend<'a> {
        self.conn.next_send()
    }

    pub async fn receive_frame(&self, frame: &mut [u8]) -> Result<(), Error> {
        self.conn.recv(frame).await
    }

    pub async fn current_link(&self) -> Option<Arc<Link>> {
        self.current_link.lock().await.upgrade()
    }

    pub async fn new_stream(
        &self,
        service: &str,
        chan: Channel,
        router: &Arc<Router>,
    ) -> Result<(), Error> {
        if let ZirconHandle::Channel(ChannelHandle { stream_ref }) = router
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
                    options: ConnectToServiceOptions {},
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
    ) -> Result<(AsyncQuicStreamWriter, AsyncQuicStreamReader), Error> {
        let io = self.conn.alloc_bidi();
        self.commands
            .as_ref()
            .unwrap()
            .clone()
            .send(ClientPeerCommand::OpenTransfer(io.0.id(), transfer_key))
            .await?;
        Ok(io)
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
        }
    }
}

async fn client_conn_stream(
    peer_node_id: NodeId,
    mut conn_stream_writer: AsyncQuicStreamWriter,
    mut conn_stream_reader: AsyncQuicStreamReader,
    mut commands: futures::channel::mpsc::Receiver<ClientPeerCommand>,
    mut services: Observer<Vec<String>>,
    conn_stats: Arc<PeerConnStats>,
) -> Result<(), Error> {
    log::info!("[{:?}] client connection stream started", peer_node_id);
    // Send FIDL header
    conn_stream_writer
        .send(&mut [0, 0, 0, fidl::encoding::MAGIC_NUMBER_INITIAL], false)
        .await
        .trace("sent fidl magic", peer_node_id)?;
    // Send config request
    let mut conn_stream_writer: FramedStreamWriter = conn_stream_writer.into();
    conn_stream_writer
        .send(FrameType::Data, &encode_fidl(&mut ConfigRequest {})?, false, &conn_stats.config)
        .await
        .trace("sent config", peer_node_id)?;
    // Receive FIDL header
    let mut fidl_hdr = [0u8; 4];
    conn_stream_reader
        .read_exact(&mut fidl_hdr)
        .await
        .context("reading FIDL header")
        .trace("received fidl header", peer_node_id)?;
    // Await config response
    let mut conn_stream_reader: FramedStreamReader = conn_stream_reader.into();
    let _ = Config::from_response(
        if let (FrameType::Data, mut bytes, false) =
            conn_stream_reader.next().await.trace("received config", peer_node_id)?
        {
            decode_fidl(&mut bytes)?
        } else {
            bail!("Failed to read config response")
        },
    );

    let on_link_status_ack = Arc::new(Mutex::new(None));
    let conn_stream_writer = Arc::new(Mutex::new(conn_stream_writer));

    let cmd_conn_stats = conn_stats.clone();
    let cmd_on_link_status_ack = on_link_status_ack.clone();
    let cmd_conn_stream_writer = conn_stream_writer.clone();
    let frm_on_link_status_ack = on_link_status_ack;
    let svc_conn_stats = conn_stats;
    let svc_conn_stream_writer = conn_stream_writer;

    let _: ((), (), ()) = futures::future::try_join3(
        async move {
            while let Some(command) = commands.next().await {
                client_conn_handle_command(
                    command,
                    &mut *cmd_conn_stream_writer.lock().await,
                    &cmd_on_link_status_ack,
                    cmd_conn_stats.clone(),
                )
                .await?;
            }
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
                            peer_node_id,
                            &mut bytes,
                            &frm_on_link_status_ack,
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
                svc_conn_stream_writer
                    .lock()
                    .await
                    .send(
                        FrameType::Data,
                        &encode_fidl(&mut PeerMessage::UpdateNodeDescription(PeerDescription {
                            services,
                        }))?,
                        false,
                        &svc_conn_stats.update_node_description,
                    )
                    .await?;
            }
        },
    )
    .await?;

    Ok(())
}

async fn client_conn_handle_command(
    command: ClientPeerCommand,
    conn_stream_writer: &mut FramedStreamWriter,
    on_link_status_ack: &Arc<Mutex<Option<futures::channel::oneshot::Sender<()>>>>,
    conn_stats: Arc<PeerConnStats>,
) -> Result<(), Error> {
    match command {
        ClientPeerCommand::UpdateLinkStatus(status, on_ack) => {
            {
                let mut on_link_status_ack = on_link_status_ack.lock().await;
                assert!(on_link_status_ack.is_none());
                *on_link_status_ack = Some(on_ack);
            }
            conn_stream_writer
                .send(
                    FrameType::Data,
                    &encode_fidl(&mut PeerMessage::UpdateLinkStatus(status))?,
                    false,
                    &conn_stats.update_link_status,
                )
                .await?;
        }
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
        ClientPeerCommand::OpenTransfer(stream_id, transfer_key) => {
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
        }
    }
    Ok(())
}

async fn client_conn_handle_incoming_frame(
    peer_node_id: NodeId,
    bytes: &mut [u8],
    on_link_status_ack: &Arc<Mutex<Option<futures::channel::oneshot::Sender<()>>>>,
) -> Result<(), Error> {
    let msg: PeerReply = decode_fidl(bytes)?;
    log::trace!("Got peer reply from {:?}: {:?}", peer_node_id, msg);
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
    }
    Ok(())
}

async fn server_conn_stream(
    node_id: NodeId,
    mut conn_stream_writer: AsyncQuicStreamWriter,
    mut conn_stream_reader: AsyncQuicStreamReader,
    router: Weak<Router>,
    peer: Weak<Peer>,
    conn_stats: Arc<PeerConnStats>,
    channel_proxy_stats: Arc<MessageStats>,
) -> Result<(), Error> {
    // Receive FIDL header
    let mut fidl_hdr = [0u8; 4];
    conn_stream_reader
        .read_exact(&mut fidl_hdr)
        .await
        .trace("server got fidl header", node_id)
        .context("reading FIDL header")?;
    let mut conn_stream_reader: FramedStreamReader = conn_stream_reader.into();
    // Send FIDL header
    conn_stream_writer
        .send(&mut [0, 0, 0, fidl::encoding::MAGIC_NUMBER_INITIAL], false)
        .await
        .trace("server sent fidl header", node_id)?;
    let mut conn_stream_writer: FramedStreamWriter = conn_stream_writer.into();
    // Await config request
    let (_, mut response) = Config::negotiate(
        if let (FrameType::Data, mut bytes, false) =
            conn_stream_reader.next().await.trace("server got config", node_id)?
        {
            decode_fidl(&mut bytes)?
        } else {
            bail!("Failed to read config response")
        },
    );
    // Send config response
    conn_stream_writer
        .send(FrameType::Data, &encode_fidl(&mut response)?, false, &conn_stats.config)
        .await
        .trace("server sent config", node_id)?;

    loop {
        let (frame_type, mut bytes, fin) = conn_stream_reader.next().await?;

        let router = Weak::upgrade(&router)
            .ok_or_else(|| format_err!("Router gone during connection stream message handling"))?;
        match frame_type {
            FrameType::Hello => bail!("Hello frames disallowed on peer connections"),
            FrameType::Control => bail!("Control frames disallowed on peer connections"),
            FrameType::Data => {
                let msg: PeerMessage = decode_fidl(&mut bytes)?;
                log::trace!(
                    "{:?} Got peer request from {:?}: {:?}",
                    router.node_id(),
                    node_id,
                    msg
                );
                match msg {
                    PeerMessage::ConnectToService(ConnectToService {
                        service_name,
                        stream_ref,
                        options: _,
                    }) => {
                        let app_channel = Channel::from_handle(
                            router
                                .clone()
                                .recv_proxied(
                                    ZirconHandle::Channel(ChannelHandle { stream_ref }),
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
                                ConnectionInfo { peer: Some(node_id.into()) },
                            )
                            .await?;
                    }
                    PeerMessage::UpdateNodeDescription(PeerDescription { services }) => {
                        router
                            .service_map()
                            .update_node(node_id, services.unwrap_or(vec![]))
                            .await?;
                    }
                    PeerMessage::UpdateLinkStatus(UpdateLinkStatus { link_status }) => {
                        let peer = Weak::upgrade(&peer)
                            .ok_or_else(|| format_err!("Peer gone during link status update"))?;
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
                                .ensure_client_peer(to, &*peer.current_link.lock().await)
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
                }
            }
        }

        if fin {
            bail!("Server connection stream closed");
        }
    }
}

async fn send_links(
    peer_node_id: NodeId,
    mut commands: futures::channel::mpsc::Sender<ClientPeerCommand>,
    mut observer: Observer<Vec<LinkStatus>>,
) -> Result<(), Error> {
    log::trace!("SEND_LINKS:{:?} BEGINS", peer_node_id);
    while let Some(link_status) = observer.next().await {
        log::trace!("SEND_LINKS:{:?} SCHEDULE SEND {:?}", peer_node_id, link_status);
        let (sender, receiver) = futures::channel::oneshot::channel();
        commands
            .send(ClientPeerCommand::UpdateLinkStatus(
                UpdateLinkStatus {
                    link_status: link_status.into_iter().map(|s| s.into()).collect(),
                },
                sender,
            ))
            .await?;
        log::trace!("SEND_LINKS:{:?} AWAIT ACK", peer_node_id);
        receiver.await?;

        log::trace!("SEND_LINKS:{:?} SLEEPY TIME", peer_node_id);
        wait_until(std::time::Instant::now() + std::time::Duration::from_secs(1)).await;
    }
    Ok(())
}
