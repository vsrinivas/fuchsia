// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    async_quic::{AsyncConnection, AsyncQuicStreamReader, AsyncQuicStreamWriter},
    channel_proxy::spawn_channel_proxy,
    coding::{decode_fidl, encode_fidl},
    framed_stream::{FrameType, FramedStreamReader, FramedStreamWriter, MessageStats},
    future_help::{log_errors, Observer},
    labels::{Endpoint, NodeId},
    link::{Link, LinkStatus},
    route_planner::{LinkDescription, RoutingUpdate},
    router::Router,
    runtime::{spawn, wait_until},
};
use anyhow::{bail, format_err, Context as _, Error};
use fidl::Channel;
use fidl_fuchsia_overnet::ConnectionInfo;
use fidl_fuchsia_overnet_protocol::{
    ConfigRequest, ConfigResponse, ConnectToService, ConnectToServiceOptions,
    PeerConnectionDiagnosticInfo, PeerDescription, PeerMessage, PeerReply, UpdateLinkStatus,
};
use futures::{lock::Mutex, pin_mut, prelude::*, select};
use std::{
    convert::TryInto,
    rc::{Rc, Weak},
    task::{Context, Poll},
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
}

#[derive(Default)]
pub struct PeerConnStats {
    pub config: MessageStats,
    pub connect_to_service: MessageStats,
    pub update_node_description: MessageStats,
    pub update_link_status: MessageStats,
    pub update_link_status_ack: MessageStats,
}

pub(crate) struct Peer {
    node_id: NodeId,
    endpoint: Endpoint,
    /// The QUIC connection itself
    conn: AsyncConnection,
    /// Current link choice for this peer
    current_link: Mutex<Weak<Link>>,
    commands: Option<futures::channel::mpsc::Sender<ClientPeerCommand>>,
    conn_stats: Rc<PeerConnStats>,
    channel_proxy_stats: Rc<MessageStats>,
}

impl Peer {
    // Common parts of new_client, new_server - create the peer object, get it routed
    fn new_instance(
        node_id: NodeId,
        conn: Box<quiche::Connection>,
        endpoint: Endpoint,
        current_link: &Weak<Link>,
        commands: Option<futures::channel::mpsc::Sender<ClientPeerCommand>>,
    ) -> Result<Rc<Self>, Error> {
        log::trace!("CREATE PEER FOR {:?}/{:?}", node_id, endpoint);
        let conn = AsyncConnection::from_connection(conn, endpoint);
        let p = Rc::new(Self {
            endpoint,
            node_id,
            conn,
            current_link: Mutex::new(current_link.clone()),
            commands,
            conn_stats: Rc::new(PeerConnStats::default()),
            channel_proxy_stats: Rc::new(MessageStats::default()),
        });
        if let Some(link) = Weak::upgrade(current_link) {
            link.add_route_for_peer(&p);
        }
        Ok(p)
    }

    /// Construct a new client peer - spawns tasks to handle making control stream requests, and
    /// publishing link metadata
    pub fn new_client(
        node_id: NodeId,
        conn: Box<quiche::Connection>,
        current_link: &Weak<Link>,
        link_status_observer: Observer<Vec<LinkStatus>>,
        service_observer: Observer<Vec<String>>,
    ) -> Result<Rc<Self>, Error> {
        let (command_sender, command_receiver) = futures::channel::mpsc::channel(1);
        let p = Self::new_instance(
            node_id,
            conn,
            Endpoint::Client,
            current_link,
            Some(command_sender.clone()),
        )?;
        let (conn_stream_writer, conn_stream_reader) = p.conn.bind_id(0);
        spawn(log_errors(
            client_conn_stream(
                node_id,
                conn_stream_writer,
                conn_stream_reader,
                command_receiver,
                service_observer,
                p.conn_stats.clone(),
            ),
            "Client connection stream failed",
        ));
        spawn(log_errors(
            send_links(command_sender, link_status_observer),
            "Sending initial link data failed",
        ));
        Ok(p)
    }

    /// Construct a new server peer - spawns tasks to handle responding to control stream requests
    pub fn new_server(
        node_id: NodeId,
        conn: Box<quiche::Connection>,
        current_link: &Weak<Link>,
        router: &Rc<Router>,
    ) -> Result<Rc<Self>, Error> {
        let p = Self::new_instance(node_id, conn, Endpoint::Server, current_link, None)?;
        let (conn_stream_writer, conn_stream_reader) = p.conn.bind_id(0);
        spawn(log_errors(
            server_conn_stream(
                node_id,
                conn_stream_writer,
                conn_stream_reader,
                Rc::downgrade(router),
                Rc::downgrade(&p),
                p.conn_stats.clone(),
                p.channel_proxy_stats.clone(),
            ),
            "Server connection stream failed",
        ));
        Ok(p)
    }

    pub async fn update_link(self: &Rc<Self>, link: Weak<Link>) {
        let mut current_link = self.current_link.lock().await;
        if let Some(link) = Weak::upgrade(&*current_link) {
            link.remove_route_for_peer(self);
        }
        *current_link = link;
        if let Some(link) = Weak::upgrade(&*current_link) {
            link.add_route_for_peer(self);
        }
    }

    pub async fn update_link_if_unset(self: &Rc<Self>, link: &Weak<Link>) {
        let mut current_link = self.current_link.lock().await;
        if Weak::upgrade(&*current_link).is_some() {
            return;
        }
        *current_link = link.clone();
        if let Some(link) = Weak::upgrade(&*current_link) {
            link.add_route_for_peer(self);
        }
    }

    pub fn poll_next_send(
        &self,
        router_node_id: NodeId,
        frame: &mut [u8],
        ctx: &mut Context<'_>,
    ) -> Poll<Result<Option<(usize, NodeId, NodeId, Endpoint)>, Error>> {
        match self.conn.poll_next_send(frame, ctx) {
            Poll::Ready(Ok(Some(n))) => {
                Poll::Ready(Ok(Some((n, router_node_id, self.node_id, self.endpoint.opposite()))))
            }
            Poll::Ready(Ok(None)) => Poll::Ready(Ok(None)),
            Poll::Ready(Err(e)) => Poll::Ready(Err(e)),
            Poll::Pending => Poll::Pending,
        }
    }

    pub fn receive_frame(&self, frame: &mut [u8]) -> Result<(), Error> {
        self.conn.recv(frame)
    }

    pub async fn current_link(&self) -> Option<Rc<Link>> {
        self.current_link.lock().await.upgrade()
    }

    pub async fn new_stream(&self, service: &str, chan: Channel) -> Result<(), Error> {
        let stream_io = self.conn.alloc_bidi();
        self.commands
            .as_ref()
            .unwrap()
            .clone()
            .send(ClientPeerCommand::ConnectToService(ConnectToService {
                service_name: service.to_string(),
                stream_id: stream_io.0.id(),
                options: ConnectToServiceOptions {},
            }))
            .await?;
        spawn_channel_proxy(chan, stream_io, self.channel_proxy_stats.clone())
    }

    pub fn diagnostics(&self, source_node_id: NodeId) -> PeerConnectionDiagnosticInfo {
        let stats = self.conn.stats();
        PeerConnectionDiagnosticInfo {
            source: Some(source_node_id.into()),
            destination: Some(self.node_id.into()),
            is_client: Some(self.endpoint == Endpoint::Client),
            is_established: Some(self.conn.is_established()),
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
    conn_stats: Rc<PeerConnStats>,
) -> Result<(), Error> {
    // Send FIDL header
    conn_stream_writer.send(&mut [0, 0, 0, fidl::encoding::MAGIC_NUMBER_INITIAL], false).await?;
    // Send config request
    let mut conn_stream_writer = FramedStreamWriter::new(conn_stream_writer);
    conn_stream_writer
        .send(FrameType::Data, &encode_fidl(&mut ConfigRequest {})?, false, &conn_stats.config)
        .await?;
    // Receive FIDL header
    let mut fidl_hdr = [0u8; 4];
    conn_stream_reader.read_exact(&mut fidl_hdr).await.context("reading FIDL header")?;
    // Await config response
    let mut conn_stream_reader = FramedStreamReader::new(conn_stream_reader);
    let _ = Config::from_response(
        if let (FrameType::Data, mut bytes, false) = conn_stream_reader.next().await? {
            decode_fidl(&mut bytes)?
        } else {
            bail!("Failed to read config response")
        },
    );

    let mut on_link_status_ack = None;

    loop {
        let mut next_command = commands.next().fuse();
        let next_frame = conn_stream_reader.next().fuse();
        let mut next_services = services.next().fuse();
        pin_mut!(next_frame);
        #[derive(Debug)]
        enum Action {
            Command(ClientPeerCommand),
            Frame(FrameType, Vec<u8>, bool),
            Services(Option<Vec<String>>),
        };
        let action = select! {
            command = next_command => {
                Action::Command(command.ok_or_else(|| format_err!("Command queue closed"))?)
            }
            frame = next_frame => {
                let (frame_type, bytes, fin) = frame?;
                Action::Frame(frame_type, bytes, fin)
            }
            services = next_services => {
                Action::Services(services)
            }
        };
        log::trace!(
            "Peer connection ->{:?} gets connection stream command: {:?}",
            peer_node_id,
            action
        );
        match action {
            Action::Command(command) => {
                client_conn_handle_command(
                    command,
                    &mut conn_stream_writer,
                    &mut on_link_status_ack,
                    conn_stats.clone(),
                )
                .await?;
            }
            Action::Frame(frame_type, mut bytes, fin) => {
                match frame_type {
                    FrameType::Data => {
                        client_conn_handle_incoming_frame(&mut bytes, &mut on_link_status_ack)
                            .await?;
                    }
                }
                if fin {
                    bail!("Client connection stream closed");
                }
            }
            Action::Services(services) => {
                conn_stream_writer
                    .send(
                        FrameType::Data,
                        &encode_fidl(&mut PeerMessage::UpdateNodeDescription(PeerDescription {
                            services,
                        }))?,
                        false,
                        &conn_stats.update_node_description,
                    )
                    .await?;
            }
        }
    }
}

async fn client_conn_handle_command(
    command: ClientPeerCommand,
    conn_stream_writer: &mut FramedStreamWriter,
    on_link_status_ack: &mut Option<futures::channel::oneshot::Sender<()>>,
    conn_stats: Rc<PeerConnStats>,
) -> Result<(), Error> {
    log::trace!("Handle client peer command: {:?}", command);
    match command {
        ClientPeerCommand::UpdateLinkStatus(status, on_ack) => {
            assert!(on_link_status_ack.is_none());
            *on_link_status_ack = Some(on_ack);
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
    }
    Ok(())
}

async fn client_conn_handle_incoming_frame(
    bytes: &mut [u8],
    on_link_status_ack: &mut Option<futures::channel::oneshot::Sender<()>>,
) -> Result<(), Error> {
    let msg: PeerReply = decode_fidl(bytes)?;
    match msg {
        PeerReply::UpdateLinkStatusAck(_) => {
            on_link_status_ack
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
    conn_stats: Rc<PeerConnStats>,
    channel_proxy_stats: Rc<MessageStats>,
) -> Result<(), Error> {
    // Receive FIDL header
    let mut fidl_hdr = [0u8; 4];
    conn_stream_reader.read_exact(&mut fidl_hdr).await.context("reading FIDL header")?;
    let mut conn_stream_reader = FramedStreamReader::new(conn_stream_reader);
    // Send FIDL header
    conn_stream_writer.send(&mut [0, 0, 0, fidl::encoding::MAGIC_NUMBER_INITIAL], false).await?;
    let mut conn_stream_writer = FramedStreamWriter::new(conn_stream_writer);
    // Await config request
    let (_, mut response) = Config::negotiate(
        if let (FrameType::Data, mut bytes, false) = conn_stream_reader.next().await? {
            decode_fidl(&mut bytes)?
        } else {
            bail!("Failed to read config response")
        },
    );
    // Send config response
    conn_stream_writer
        .send(FrameType::Data, &encode_fidl(&mut response)?, false, &conn_stats.config)
        .await?;

    loop {
        let (frame_type, mut bytes, fin) = conn_stream_reader.next().await?;

        let router = Weak::upgrade(&router)
            .ok_or_else(|| format_err!("Router gone during connection stream message handling"))?;
        match frame_type {
            FrameType::Data => {
                let msg: PeerMessage = decode_fidl(&mut bytes)?;
                log::trace!("Got peer request from {:?}: {:?}", node_id, msg);
                match msg {
                    PeerMessage::ConnectToService(ConnectToService {
                        service_name,
                        stream_id: stream_index,
                        options: _,
                    }) => {
                        let (overnet_channel, app_channel) = Channel::create()?;
                        router
                            .service_map()
                            .connect(
                                &service_name,
                                app_channel,
                                ConnectionInfo { peer: Some(node_id.into()) },
                            )
                            .await?;
                        // Spawn after so that we don't use the local kernel's memory for
                        // buffering messages (ideally we have flow control in the kernel and so
                        // this could be prior and we could likely remove a context switch for
                        // some pipelined operations)
                        spawn_channel_proxy(
                            overnet_channel,
                            conn_stream_writer.underlying_quic_connection().bind_id(stream_index),
                            channel_proxy_stats.clone(),
                        )?;
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
                            status.push((
                                to,
                                local_id.into(),
                                LinkDescription {
                                    round_trip_time: std::time::Duration::from_micros(
                                        metrics.round_trip_time.unwrap_or(std::u64::MAX),
                                    ),
                                },
                            ));
                        }
                        router
                            .send_routing_update(RoutingUpdate::UpdateRemoteLinkStatus {
                                from_node_id: node_id,
                                status,
                            })
                            .await;
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
    mut commands: futures::channel::mpsc::Sender<ClientPeerCommand>,
    mut observer: Observer<Vec<LinkStatus>>,
) -> Result<(), Error> {
    while let Some(link_status) = observer.next().await {
        let (sender, receiver) = futures::channel::oneshot::channel();
        commands
            .send(ClientPeerCommand::UpdateLinkStatus(
                UpdateLinkStatus {
                    link_status: link_status.into_iter().map(|s| s.into()).collect(),
                },
                sender,
            ))
            .await?;
        receiver.await?;

        wait_until(std::time::Instant::now() + std::time::Duration::from_secs(1)).await;
    }
    Ok(())
}
