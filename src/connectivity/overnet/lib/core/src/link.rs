// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A `Link` describes an established communications channel between two nodes.

use crate::{
    async_quic::NextSend,
    future_help::{log_errors, Observer, PollMutex},
    labels::{Endpoint, NodeId, NodeLinkId},
    link_frame_label::{LinkFrameLabel, RoutingTarget, LINK_FRAME_LABEL_MAX_SIZE},
    peer::Peer,
    ping_tracker::{PingSender, PingTracker},
    router::Router,
};
use anyhow::{bail, format_err, Context as _, Error};
use fidl_fuchsia_overnet_protocol::{LinkDiagnosticInfo, LinkMetrics};
use fuchsia_async::Task;
use futures::{
    future::poll_fn,
    lock::{Mutex, MutexGuard},
    ready,
};
use rental::*;
use std::{
    collections::{HashSet, VecDeque},
    convert::TryInto,
    sync::atomic::{AtomicU64, Ordering},
    sync::Arc,
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
    GatherPeer::new(peer.clone(), |peer| peer.next_send())
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

/// Runner for a link - once this goes, the link is shutdown
/// NOTE: Router never holds an Arc<LinkRunner>
struct LinkRunner {
    ping_tracker: PingTracker,
    router: Arc<Router>,
    // Maintenance tasks for the link - once the link is dropped, these should stop.
    _task: Task<()>,
}

/// Routing data gor a link
pub(crate) struct LinkRouting {
    own_node_id: NodeId,
    peer_node_id: NodeId,
    node_link_id: NodeLinkId,
    gatherer: Mutex<PacketGatherer>,
    stats: LinkStats,
    rtt_observer: Mutex<Observer<Option<Duration>>>,
}

/// Sender for a link
pub struct LinkSender {
    runner: Arc<LinkRunner>,
    routing: Arc<LinkRouting>,
}

/// Receiver for a link
pub struct LinkReceiver {
    runner: Arc<LinkRunner>,
    routing: Arc<LinkRouting>,
}

impl std::fmt::Debug for LinkRouting {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "LINK({:?}:{:?}->{:?})", self.node_link_id, self.own_node_id, self.peer_node_id)
    }
}

pub(crate) fn new_link(
    peer_node_id: NodeId,
    node_link_id: NodeLinkId,
    router: &Arc<Router>,
) -> (LinkSender, LinkReceiver, Arc<LinkRouting>, Observer<Option<Duration>>) {
    let (ping_tracker, observer1, observer2) = PingTracker::new();
    let pt_run = log_errors(ping_tracker.run(), "ping tracker failed");
    let routing = Arc::new(LinkRouting {
        own_node_id: router.node_id(),
        peer_node_id,
        node_link_id,
        gatherer: Mutex::new(PacketGatherer {
            forward_packets: VecDeque::new(),
            peers: Vec::new(),
            waker: None,
        }),
        rtt_observer: Mutex::new(observer2),
        stats: LinkStats {
            packets_forwarded: AtomicU64::new(0),
            pings_sent: AtomicU64::new(0),
            received_bytes: AtomicU64::new(0),
            sent_bytes: AtomicU64::new(0),
            received_packets: AtomicU64::new(0),
            sent_packets: AtomicU64::new(0),
        },
    });
    let runner =
        Arc::new(LinkRunner { ping_tracker, router: router.clone(), _task: Task::spawn(pt_run) });
    (
        LinkSender { routing: routing.clone(), runner: runner.clone() },
        LinkReceiver { routing: routing.clone(), runner: runner.clone() },
        routing,
        observer1,
    )
}

impl LinkRouting {
    pub(crate) fn id(&self) -> NodeLinkId {
        self.node_link_id
    }

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
                .rtt_observer
                .lock()
                .await
                .peek()
                .await
                .flatten()
                .map(|d| d.as_micros().try_into().unwrap_or(std::u64::MAX)),
        }
    }

    pub(crate) async fn add_route_for_peers(&self, peers: impl Iterator<Item = &Arc<Peer>>) {
        let mut g = self.gatherer.lock().await;
        g.peers.extend(peers.map(|peer| gather_from(peer.clone())));
        log::trace!(
            "[{:?}] add_route_for_peers gives peers: {:?}",
            self.debug_id(),
            g.peers.iter().map(|p| p.all().peer.debug_id()).collect::<Vec<_>>()
        );
        g.waker.take().map(|w| w.wake());
    }

    pub(crate) async fn remove_route_for_peers(&self, peers: impl Iterator<Item = &Arc<Peer>>) {
        let remove: HashSet<_> = peers.map(|peer| peer.conn_id()).collect();
        let mut g = self.gatherer.lock().await;
        g.peers.retain(move |other| !remove.contains(&other.all().peer.conn_id()));
        log::trace!(
            "[{:?}] remove_route_for_peers gives peers: {:?}",
            self.debug_id(),
            g.peers.iter().map(|p| p.all().peer.debug_id()).collect::<Vec<_>>()
        );
    }

    async fn forward(&self, routing_label: RoutingTarget, frame: &mut [u8]) -> Result<(), Error> {
        if routing_label.src == self.peer_node_id {
            bail!(
                "Loopback detected, dropping packet [routing_label={:?} link={:?}]",
                routing_label,
                self.debug_id()
            );
        }
        let mut g = self.gatherer.lock().await;
        if g.forward_packets.is_empty() {
            g.waker.take().map(|w| w.wake());
        }
        g.forward_packets.push_back((routing_label, frame.to_vec()));
        Ok(())
    }
}

impl LinkReceiver {
    /// Report a packet was received.
    /// An error processing a packet does not indicate that the link should be closed.
    pub async fn received_packet(&self, packet: &mut [u8]) -> Result<(), Error> {
        let runner = &*self.runner;
        let routing = &*self.routing;
        let (routing_label, packet) = {
            routing.stats.received_packets.fetch_add(1, Ordering::Relaxed);
            routing.stats.received_bytes.fetch_add(packet.len() as u64, Ordering::Relaxed);
            if packet.len() < 1 {
                bail!("Received empty packet");
            }
            let (routing_label, packet_length) =
                LinkFrameLabel::decode(routing.peer_node_id, routing.own_node_id, packet)
                    .with_context(|| {
                        format_err!(
                            "Decoding routing label because {:?} received packet from {:?}",
                            routing.own_node_id,
                            routing.peer_node_id,
                        )
                    })?;
            log::trace!(
                "{:?} routing_label={:?} packet_length={} src_len={}",
                routing.own_node_id,
                routing_label,
                packet_length,
                packet.len()
            );
            let packet = &mut packet[..packet_length];
            if routing_label.target.src == routing.own_node_id {
                // Got a packet that was sourced here: break the infinite loop by definitely not
                // processing it.
                bail!(
                    "[{:?}] Received looped packet; routing_label={:?} packet_length={}",
                    routing.debug_id(),
                    routing_label,
                    packet_length
                );
            }
            if let Some(ping) = routing_label.ping {
                runner.ping_tracker.got_ping(ping).await;
            }
            if let Some(pong) = routing_label.pong {
                runner.ping_tracker.got_pong(pong).await;
            }
            if packet.len() == 0 {
                // Packet was just control bits
                return Ok(());
            }

            (routing_label, packet)
        };

        runner
            .router
            .ensure_client_peer(
                routing_label.target.src,
                self.routing.clone(),
                "forwarding from src",
            )
            .await?;

        if routing_label.target.dst == routing.own_node_id {
            let hdr =
                quiche::Header::from_slice(packet, quiche::MAX_CONN_ID_LEN).with_context(|| {
                    format!(
                        "Decoding quic header; link={:?}; routing_label={:?}",
                        routing.debug_id(),
                        routing_label
                    )
                })?;
            log::trace!("{:?} handle packet with header {:?}", routing.own_node_id, hdr);
            let peer = runner
                .router
                .lookup_peer(
                    &hdr.dcid,
                    hdr.ty,
                    routing_label.target.src,
                    Arc::downgrade(&self.routing),
                )
                .await
                .with_context(|| {
                    format!(
                        "link={:?}; routing_label={:?}; hdr={:?}",
                        routing.debug_id(),
                        routing_label,
                        hdr
                    )
                })?;
            log::trace!(
                "{:?} handle with peer {:?} (routing_label: {:?}  hdr: {:?})",
                routing.own_node_id,
                peer.debug_id(),
                routing_label,
                hdr
            );
            peer.receive_frame(packet).await.with_context(|| {
                format!(
                    concat!(
                        "Receiving packet on quic connection;",
                        " peer node={:?} endpoint={:?};",
                        " link node={:?} peer={:?};",
                        " hdr={:?}"
                    ),
                    peer.node_id(),
                    peer.endpoint(),
                    routing.own_node_id,
                    routing.peer_node_id,
                    hdr,
                )
            })?;
        } else {
            if let Some(link) = runner.router.peer_link(routing_label.target.dst).await {
                log::trace!(
                    "{:?} Forward packet via {:?}: routing_label={:?}, packet_len={}",
                    routing.debug_id(),
                    link.debug_id(),
                    routing_label,
                    packet.len()
                );
                link.forward(routing_label.target, packet).await?;
            } else {
                log::trace!(
                    "{:?} Drop unforwardable packet (no link): routing_label={:?}, packet_len={}",
                    routing.debug_id(),
                    routing_label,
                    packet.len()
                );
            }
        }

        Ok(())
    }
}

impl LinkSender {
    pub(crate) fn router(&self) -> &Arc<Router> {
        &self.runner.router
    }

    /// Retrieve the next frame that should be sent via this link.
    /// Returns: Ok(Some(n)) to send a packet of length n
    ///          Ok(None) to cleanly indicate link closure
    ///          Err(e) to indicate error
    pub async fn next_send(&self, mut frame: &mut [u8]) -> Result<Option<usize>, Error> {
        const MAX_FRAME_LENGTH: usize = 1400;
        assert!(frame.len() >= MAX_FRAME_LENGTH);
        if frame.len() > MAX_FRAME_LENGTH {
            frame = &mut frame[..MAX_FRAME_LENGTH];
        }

        let mut lock = PollMutex::new(&self.routing.gatherer);
        let mut ping_sender = PingSender::new(&self.runner.ping_tracker);
        poll_fn(|ctx| self.poll_next_send(ctx, frame, &mut lock, &mut ping_sender)).await
    }

    fn poll_next_packet(
        &self,
        ctx: &mut Context<'_>,
        frame: &mut [u8],
        mut g: MutexGuard<'_, PacketGatherer>,
    ) -> Poll<Result<Option<(RoutingTarget, usize)>, Error>> {
        let runner = &*self.runner;
        let routing = &*self.routing;
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
                    log::trace!("[{:?}] peer is closed", peer.debug_id());
                    let router = runner.router.clone();
                    let peer_node_id = routing.peer_node_id;
                    let weak_link = Arc::downgrade(&self.routing);
                    let is_client_peer_for_link =
                        peer.endpoint() == Endpoint::Client && peer_node_id == peer.node_id();
                    // TODO: maybe don't detach?
                    Task::spawn(log_errors(
                        async move {
                            router.remove_peer(peer.conn_id()).await;
                            if is_client_peer_for_link {
                                router
                                    .ensure_client_peer(
                                        peer_node_id,
                                        weak_link,
                                        "respawn_client_peer_for_link",
                                    )
                                    .await?;
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
                    let target = RoutingTarget { src: routing.own_node_id, dst: peer.node_id() };
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
        &self,
        ctx: &mut Context<'_>,
        frame: &mut [u8],
        lock: &mut PollMutex<'_, PacketGatherer>,
        ping_sender: &mut PingSender<'_>,
    ) -> Poll<Result<Option<usize>, Error>> {
        let routing = &*self.routing;
        let g = ready!(lock.poll(ctx));
        let (ping, pong) = ping_sender.poll(ctx);
        let max_packet_len = frame.len() - LINK_FRAME_LABEL_MAX_SIZE;
        match self.poll_next_packet(ctx, &mut frame[..max_packet_len], g) {
            Poll::Ready(Err(e)) => Poll::Ready(Err(e)),
            Poll::Ready(Ok(None)) => Poll::Ready(Ok(None)),
            Poll::Ready(Ok(Some((target, n)))) => {
                if ping.is_some() {
                    routing.stats.pings_sent.fetch_add(1, Ordering::Relaxed);
                }
                assert_ne!(target.src, routing.peer_node_id);
                let label = LinkFrameLabel {
                    target,
                    ping,
                    pong,
                    debug_token: LinkFrameLabel::new_debug_token(),
                };
                log::trace!("link {:?} deliver {:?}", routing.debug_id(), label);
                let n_tail = label.encode_for_link(
                    routing.own_node_id,
                    routing.peer_node_id,
                    &mut frame[n..],
                )?;
                routing.stats.sent_packets.fetch_add(1, Ordering::Relaxed);
                routing.stats.sent_bytes.fetch_add((n + n_tail) as u64, Ordering::Relaxed);
                Poll::Ready(Ok(Some(n + n_tail)))
            }
            Poll::Pending => {
                if ping.is_some() || pong.is_some() {
                    if ping.is_some() {
                        routing.stats.pings_sent.fetch_add(1, Ordering::Relaxed);
                    }
                    let label = LinkFrameLabel {
                        target: RoutingTarget {
                            src: routing.own_node_id,
                            dst: routing.peer_node_id,
                        },
                        ping,
                        pong,
                        debug_token: LinkFrameLabel::new_debug_token(),
                    };
                    log::trace!("link {:?} deliver {:?}", routing.debug_id(), label);
                    let n =
                        label.encode_for_link(routing.own_node_id, routing.peer_node_id, frame)?;
                    routing.stats.sent_packets.fetch_add(1, Ordering::Relaxed);
                    routing.stats.sent_bytes.fetch_add(n as u64, Ordering::Relaxed);
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
