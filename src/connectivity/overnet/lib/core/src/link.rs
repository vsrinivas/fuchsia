// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    async_quic::NextSend,
    future_help::{log_errors, Observer, PollMutex},
    labels::{Endpoint, NodeId, NodeLinkId},
    link_frame_label::{LinkFrameLabel, RoutingTarget, LINK_FRAME_LABEL_MAX_SIZE},
    peer::Peer,
    ping_tracker::PingTracker,
    router::Router,
    runtime::Task,
};
use anyhow::{bail, format_err, Context as _, Error};
use fidl_fuchsia_overnet_protocol::{LinkDiagnosticInfo, LinkMetrics};
use futures::{
    future::poll_fn,
    lock::{Mutex, MutexGuard},
    ready,
};
use rental::*;
use std::{
    collections::VecDeque,
    convert::TryInto,
    sync::atomic::{AtomicU64, Ordering},
    sync::{Arc, Weak},
    task::{Context, Poll, Waker},
    time::Duration,
};

rental! {
    mod rentals {
        use super::*;

        #[rental(covariant)]
        pub(super) struct GatherPeer {
            peer: Arc<Peer>,
            next_send: NextSend<'peer>,
        }
    }
}

use rentals::GatherPeer;

fn gather_from(peer: Arc<Peer>) -> GatherPeer {
    GatherPeer::new(peer, |peer| peer.next_send())
}

struct PacketGatherer {
    forward_packets: VecDeque<(RoutingTarget, Vec<u8>)>,
    peers: Vec<GatherPeer>,
    waker: Option<Waker>,
}

impl std::fmt::Debug for PacketGatherer {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        "PacketGatherer".fmt(f)
    }
}

struct LinkStats {
    packets_forwarded: AtomicU64,
    pings_sent: AtomicU64,
    received_bytes: AtomicU64,
    sent_bytes: AtomicU64,
    received_packets: AtomicU64,
    sent_packets: AtomicU64,
}

/// A `Link` describes an established communications channel between two nodes.
pub struct Link {
    own_node_id: NodeId,
    peer_node_id: NodeId,
    node_link_id: NodeLinkId,
    ping_tracker: PingTracker,
    router: Arc<Router>,
    gatherer: Mutex<PacketGatherer>,
    stats: LinkStats,
    _task: Task,
}

impl std::fmt::Debug for Link {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "LINK({:?}:{:?}->{:?})", self.node_link_id, self.own_node_id, self.peer_node_id)
    }
}

impl Link {
    pub(crate) async fn new(
        peer_node_id: NodeId,
        node_link_id: NodeLinkId,
        router: &Arc<Router>,
    ) -> Result<Arc<Link>, Error> {
        let ping_tracker = PingTracker::new();
        let pt_run = log_errors(ping_tracker.run(), "ping tracker failed");
        let link = Arc::new(Link {
            own_node_id: router.node_id(),
            peer_node_id,
            node_link_id,
            ping_tracker,
            stats: LinkStats {
                packets_forwarded: AtomicU64::new(0),
                pings_sent: AtomicU64::new(0),
                received_bytes: AtomicU64::new(0),
                sent_bytes: AtomicU64::new(0),
                received_packets: AtomicU64::new(0),
                sent_packets: AtomicU64::new(0),
            },
            router: router.clone(),
            gatherer: Mutex::new(PacketGatherer {
                forward_packets: VecDeque::new(),
                peers: Vec::new(),
                waker: None,
            }),
            _task: Task::spawn(pt_run),
        });
        let weak_link = Arc::downgrade(&link);
        router
            .client_peer(peer_node_id, &weak_link)
            .await
            .context("creating client peer for link")?
            .update_link_if_unset(&weak_link)
            .await;
        if let Some(peer) = router.server_peer(peer_node_id, false, &Weak::new()).await? {
            peer.update_link_if_unset(&weak_link).await;
        }
        Ok(link)
    }

    pub(crate) async fn new_round_trip_time_observer(&self) -> Observer<Option<Duration>> {
        self.ping_tracker.new_round_trip_time_observer().await
    }

    pub(crate) fn id(&self) -> NodeLinkId {
        self.node_link_id
    }

    #[cfg(test)]
    pub(crate) fn own_node_id(&self) -> NodeId {
        self.own_node_id
    }

    #[cfg(test)]
    pub(crate) fn peer_node_id(&self) -> NodeId {
        self.peer_node_id
    }

    pub(crate) fn debug_id(&self) -> impl std::fmt::Debug {
        (self.node_link_id, self.own_node_id, self.peer_node_id)
    }

    pub(crate) async fn diagnostic_info(&self) -> LinkDiagnosticInfo {
        let stats = &self.stats;
        LinkDiagnosticInfo {
            source: Some(self.own_node_id.into()),
            destination: Some(self.peer_node_id.into()),
            source_local_id: Some(self.node_link_id.0),
            sent_packets: Some(stats.sent_packets.load(Ordering::Relaxed)),
            received_packets: Some(stats.received_packets.load(Ordering::Relaxed)),
            sent_bytes: Some(stats.sent_bytes.load(Ordering::Relaxed)),
            received_bytes: Some(stats.received_bytes.load(Ordering::Relaxed)),
            pings_sent: Some(stats.pings_sent.load(Ordering::Relaxed)),
            packets_forwarded: Some(stats.packets_forwarded.load(Ordering::Relaxed)),
            round_trip_time_microseconds: self
                .ping_tracker
                .round_trip_time()
                .await
                .map(|rtt| rtt.as_micros().try_into().unwrap_or(std::u64::MAX)),
        }
    }

    pub(crate) async fn add_route_for_peer(&self, peer: &Arc<Peer>) {
        let mut g = self.gatherer.lock().await;
        for other in g.peers.iter() {
            assert_ne!(peer.id(), other.all().peer.id());
        }
        g.peers.push(gather_from(peer.clone()));
        g.waker.take().map(|w| w.wake());
    }

    pub(crate) async fn remove_route_for_peer(&self, peer: (NodeId, Endpoint)) {
        self.gatherer.lock().await.peers.retain(|other| other.all().peer.id() != peer);
    }

    /// Report a packet was received.
    /// An error processing a packet does not indicate that the link should be closed.
    pub async fn received_packet(self: &Arc<Self>, packet: &mut [u8]) -> Result<(), Error> {
        let (routing_label, packet) = {
            self.stats.received_packets.fetch_add(1, Ordering::Relaxed);
            self.stats.received_bytes.fetch_add(packet.len() as u64, Ordering::Relaxed);
            if packet.len() < 1 {
                bail!("Received empty packet");
            }
            let (routing_label, packet_length) =
                LinkFrameLabel::decode(self.peer_node_id, self.own_node_id, packet).with_context(
                    || {
                        format_err!(
                            "Decoding routing label because {:?} received packet from {:?}",
                            self.own_node_id,
                            self.peer_node_id,
                        )
                    },
                )?;
            log::trace!(
                "{:?} routing_label={:?} packet_length={} src_len={}",
                self.own_node_id,
                routing_label,
                packet_length,
                packet.len()
            );
            let packet = &mut packet[..packet_length];
            if routing_label.target.src == self.own_node_id {
                // Got a packet that was sourced here: break the infinite loop by definitely not
                // processing it.
                bail!(
                    "Received looped packet; routing_label={:?} packet_length={}",
                    routing_label,
                    packet_length
                );
            }
            if let Some(ping) = routing_label.ping {
                self.ping_tracker.got_ping(ping).await;
            }
            if let Some(pong) = routing_label.pong {
                self.ping_tracker.got_pong(pong).await;
            }
            if packet.len() == 0 {
                // Packet was just control bits
                return Ok(());
            }

            (routing_label, packet)
        };

        if routing_label.target.dst == self.own_node_id {
            let peer = match routing_label.target.endpoint {
                Endpoint::Server => {
                    let hdr = quiche::Header::from_slice(packet, quiche::MAX_CONN_ID_LEN)
                        .context("Decoding quic header")?;

                    if hdr.ty == quiche::Type::VersionNegotiation {
                        bail!("Version negotiation invalid on the server");
                    }

                    // If we're asked for a server connection, we should create a client connection
                    self.router
                        .ensure_client_peer(routing_label.target.src, &Arc::downgrade(self))
                        .await
                        .context("Handling server packet to self")?;

                    if let Some(server_peer) = self
                        .router
                        .server_peer(
                            routing_label.target.src,
                            hdr.ty == quiche::Type::Initial,
                            &Arc::downgrade(self),
                        )
                        .await?
                    {
                        server_peer
                    } else {
                        bail!("No server for link, and not an Initial packet");
                    }
                }
                Endpoint::Client => self
                    .router
                    .client_peer(routing_label.target.src, &Arc::downgrade(self))
                    .await
                    .context("Routing packet to own client")?,
            };
            peer.receive_frame(packet).await.with_context(|| format_err!("Receiving packet on quic connection; peer node={:?} endpoint={:?}; link node={:?} peer={:?}", peer.node_id(), peer.endpoint(), self.own_node_id, self.peer_node_id))?;
        } else {
            // If we're asked to forward a packet, we should have client connections to each end
            self.router
                .ensure_client_peer(routing_label.target.src, &Arc::downgrade(self))
                .await
                .context("Forwarding packet source summoning")?;
            // Just need to get the peer's current_link, doesn't matter server or client, so grab client because it's not optional
            let peer = self
                .router
                .client_peer(routing_label.target.dst, &Arc::downgrade(self))
                .await
                .context("Routing packet to other client")?;
            if let Some(link) = peer.current_link().await {
                log::trace!(
                    "{:?} Forward packet: routing_label={:?}, packet_len={}",
                    self.own_node_id,
                    routing_label,
                    packet.len()
                );
                link.forward(routing_label.target, packet).await;
            } else {
                log::info!(
                    "{:?} Drop unforwardable packet (no link): routing_label={:?}, packet_len={}",
                    self.own_node_id,
                    routing_label,
                    packet.len()
                );
            }
        }

        Ok(())
    }

    async fn forward(&self, routing_label: RoutingTarget, frame: &mut [u8]) {
        let mut g = self.gatherer.lock().await;
        if g.forward_packets.is_empty() {
            g.waker.take().map(|w| w.wake());
        }
        g.forward_packets.push_back((routing_label, frame.to_vec()));
    }

    /// Retrieve the next frame that should be sent via this link.
    /// Returns: Ok(Some(n)) to send a packet of length n
    ///          Ok(None) to cleanly indicate link closure
    ///          Err(e) to indicate error
    pub async fn next_send(self: &Arc<Self>, frame: &mut [u8]) -> Result<Option<usize>, Error> {
        let mut lock = PollMutex::new(&self.gatherer);
        poll_fn(|ctx| self.poll_next_send(ctx, frame, &mut lock)).await
    }

    fn poll_next_packet(
        self: &Arc<Self>,
        ctx: &mut Context<'_>,
        frame: &mut [u8],
        mut g: MutexGuard<'_, PacketGatherer>,
    ) -> Poll<Result<Option<(RoutingTarget, usize)>, Error>> {
        loop {
            if let Some((target, forward)) = g.forward_packets.pop_front() {
                let n = forward.len();
                if n > frame.len() {
                    log::warn!(
                        "Dropping too long packet ({} bytes vs {} send buffer)",
                        n,
                        frame.len()
                    );
                    continue;
                }
                (&mut frame[..n]).copy_from_slice(&forward);
                return Poll::Ready(Ok(Some((target, n))));
            }
            let item = g.peers.iter_mut().enumerate().find_map(|(i, peer)| {
                match peer.rent_mut(|next_send| next_send.poll(ctx, frame)) {
                    Poll::Ready(r) => Some((i, r)),
                    Poll::Pending => None,
                }
            });
            match item {
                Some((_, Err(e))) => return Poll::Ready(Err(e)),
                Some((i, Ok(None))) => {
                    let peer = g.peers.swap_remove(i).into_head();
                    let router = self.router.clone();
                    let peer_node_id = self.peer_node_id;
                    let weak_link = Arc::downgrade(self);
                    let is_client_peer_for_link =
                        peer.endpoint() == Endpoint::Client && peer_node_id == peer.node_id();
                    // TODO: maybe don't detach?
                    Task::spawn(log_errors(
                        async move {
                            router.remove_peer(peer).await?;
                            if is_client_peer_for_link {
                                router
                                    .client_peer(peer_node_id, &weak_link)
                                    .await
                                    .context("creating client peer for link")?
                                    .update_link_if_unset(&weak_link)
                                    .await;
                            }
                            Ok(())
                        },
                        "Removing peer",
                    ))
                    .detach();
                    continue;
                }
                Some((i, Ok(Some(n)))) => {
                    let peer = g.peers.swap_remove(i).into_head();
                    let target = RoutingTarget {
                        src: self.own_node_id,
                        dst: peer.node_id(),
                        endpoint: peer.endpoint().opposite(),
                    };
                    g.peers.push(gather_from(peer));
                    return Poll::Ready(Ok(Some((target, n))));
                }
                None => {
                    g.waker = Some(ctx.waker().clone());
                    return Poll::Pending;
                }
            };
        }
    }

    /// Fetch the next frame that should be sent by the link. Returns Ok(None) on link
    /// closure, Ok(Some(packet_length)) on successful read, and an error otherwise.
    fn poll_next_send(
        self: &Arc<Self>,
        ctx: &mut Context<'_>,
        frame: &mut [u8],
        lock: &mut PollMutex<'_, PacketGatherer>,
    ) -> Poll<Result<Option<usize>, Error>> {
        let g = ready!(lock.poll(ctx));
        let (ping, pong) = self.ping_tracker.poll_send_ping_pong(ctx);
        let max_packet_len = frame.len() - LINK_FRAME_LABEL_MAX_SIZE;
        match self.poll_next_packet(ctx, &mut frame[..max_packet_len], g) {
            Poll::Ready(Err(e)) => Poll::Ready(Err(e)),
            Poll::Ready(Ok(None)) => Poll::Ready(Ok(None)),
            Poll::Ready(Ok(Some((target, n)))) => {
                if ping.is_some() {
                    self.stats.pings_sent.fetch_add(1, Ordering::Relaxed);
                }
                let n_tail = LinkFrameLabel {
                    target,
                    ping,
                    pong,
                    debug_token: LinkFrameLabel::new_debug_token(),
                }
                .encode_for_link(
                    self.own_node_id,
                    self.peer_node_id,
                    &mut frame[n..],
                )?;
                self.stats.sent_packets.fetch_add(1, Ordering::Relaxed);
                self.stats.sent_bytes.fetch_add((n + n_tail) as u64, Ordering::Relaxed);
                Poll::Ready(Ok(Some(n + n_tail)))
            }
            Poll::Pending => {
                if ping.is_some() || pong.is_some() {
                    if ping.is_some() {
                        self.stats.pings_sent.fetch_add(1, Ordering::Relaxed);
                    }
                    let n = LinkFrameLabel {
                        target: RoutingTarget {
                            src: self.own_node_id,
                            dst: self.peer_node_id,
                            endpoint: Endpoint::Client,
                        },
                        ping,
                        pong,
                        debug_token: LinkFrameLabel::new_debug_token(),
                    }
                    .encode_for_link(
                        self.own_node_id,
                        self.peer_node_id,
                        frame,
                    )?;
                    self.stats.sent_packets.fetch_add(1, Ordering::Relaxed);
                    self.stats.sent_bytes.fetch_add(n as u64, Ordering::Relaxed);
                    Poll::Ready(Ok(Some(n)))
                } else {
                    Poll::Pending
                }
            }
        }
    }
}

#[derive(Clone, Debug)]
pub struct LinkStatus {
    pub(crate) local_id: NodeLinkId,
    pub(crate) to: NodeId,
    pub(crate) round_trip_time: Duration,
}

impl From<LinkStatus> for fidl_fuchsia_overnet_protocol::LinkStatus {
    fn from(status: LinkStatus) -> fidl_fuchsia_overnet_protocol::LinkStatus {
        fidl_fuchsia_overnet_protocol::LinkStatus {
            local_id: status.local_id.0,
            to: status.to.into(),
            metrics: LinkMetrics {
                round_trip_time: Some(
                    status.round_trip_time.as_micros().try_into().unwrap_or(std::u64::MAX),
                ),
            },
        }
    }
}
