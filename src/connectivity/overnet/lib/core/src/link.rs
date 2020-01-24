// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    future_help::{Observable, Observer},
    labels::{Endpoint, NodeId, NodeLinkId},
    peer::Peer,
    ping_tracker::{PingTracker, PingTrackerResult, PongTracker},
    route_planner::LinkDescription,
    router::Router,
    routing_label::{RoutingLabel, MAX_ROUTING_LABEL_LENGTH},
    runtime::{spawn, wait_until},
};
use anyhow::{bail, format_err, Context as _, Error};
use fidl_fuchsia_overnet_protocol::{LinkDiagnosticInfo, LinkMetrics};
use futures::prelude::*;
use std::{
    cell::RefCell,
    collections::VecDeque,
    convert::TryInto,
    pin::Pin,
    rc::{Rc, Weak},
    task::{Context, Poll, Waker},
    time::Instant,
};

struct LinkState {
    ping_tracker: PingTracker,
    pong_tracker: PongTracker,
    route_for_peers: Vec<Weak<Peer>>,
    waiting_for_send: Option<Waker>,
    timeout_generation: u64,
    to_forward: VecDeque<(RoutingLabel, Vec<u8>)>,
    //---- Stats ----
    packets_forwarded: u64,
    pings_sent: u64,
    received_bytes: u64,
    sent_bytes: u64,
    received_packets: u64,
    sent_packets: u64,
}

/// A `Link` describes an established communications channel between two nodes.
pub struct Link {
    own_node_id: NodeId,
    peer_node_id: NodeId,
    node_link_id: NodeLinkId,
    router: Rc<Router>,
    description: Observable<Option<LinkDescription>>,
    state: RefCell<LinkState>,
}

impl Link {
    pub(crate) async fn new(
        peer_node_id: NodeId,
        node_link_id: NodeLinkId,
        router: &Rc<Router>,
    ) -> Result<Rc<Link>, Error> {
        let (ping_tracker, ptres) = PingTracker::new();
        let link = Rc::new(Link {
            own_node_id: router.node_id(),
            peer_node_id,
            node_link_id,
            state: RefCell::new(LinkState {
                ping_tracker,
                pong_tracker: PongTracker::new(),
                route_for_peers: Vec::new(),
                waiting_for_send: None,
                to_forward: VecDeque::new(),
                timeout_generation: 0,
                // Stats
                packets_forwarded: 0,
                pings_sent: 0,
                received_bytes: 0,
                sent_bytes: 0,
                received_packets: 0,
                sent_packets: 0,
            }),
            router: router.clone(),
            description: Observable::new(None),
        });
        link.handle_ping_tracker_result(ptres).await;
        let weak_link = Rc::downgrade(&link);
        router
            .client_peer(peer_node_id, &weak_link)
            .await
            .context("creating client peer for link")?
            .update_link_if_unset(&weak_link)
            .await;
        if let Some(peer) = router.server_peer(peer_node_id, false, &Weak::new()).await? {
            peer.update_link_if_unset(&weak_link).await;
        }
        router.add_link(&link).await;
        Ok(link)
    }

    pub(crate) fn id(&self) -> NodeLinkId {
        self.node_link_id
    }

    pub(crate) fn diagnostic_info(&self) -> LinkDiagnosticInfo {
        let state = self.state.borrow();
        LinkDiagnosticInfo {
            source: Some(self.own_node_id.into()),
            destination: Some(self.peer_node_id.into()),
            source_local_id: Some(self.node_link_id.0),
            sent_packets: Some(state.sent_packets),
            received_packets: Some(state.received_packets),
            sent_bytes: Some(state.sent_bytes),
            received_bytes: Some(state.received_bytes),
            pings_sent: Some(state.pings_sent),
            packets_forwarded: Some(state.packets_forwarded),
            round_trip_time_microseconds: state
                .ping_tracker
                .round_trip_time()
                .map(|rtt| rtt.as_micros().try_into().unwrap_or(std::u64::MAX)),
        }
    }

    pub(crate) fn add_route_for_peer(&self, peer: &Rc<Peer>) {
        let mut state = self.state.borrow_mut();
        state.route_for_peers.push(Rc::downgrade(peer));
        state.wake_send();
    }

    pub(crate) fn remove_route_for_peer(&self, peer: &Rc<Peer>) {
        self.state.borrow_mut().route_for_peers.retain(|other| {
            Weak::upgrade(other).map(|other| Rc::ptr_eq(peer, &other)).unwrap_or(false)
        });
    }

    /// Report a packet was received.
    /// An error processing a packet does not indicate that the link should be closed.
    pub async fn received_packet(self: &Rc<Self>, packet: &mut [u8]) -> Result<(), Error> {
        let mut state = self.state.borrow_mut();

        state.received_packets += 1;
        state.received_bytes += packet.len() as u64;

        if packet.len() < 1 {
            bail!("Received empty packet");
        }

        let (routing_label, packet_length) =
            RoutingLabel::decode(self.peer_node_id, self.own_node_id, packet).with_context(
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

        if let Some(ping) = routing_label.ping {
            if state.pong_tracker.got_ping(ping) {
                state.wake_send();
            }
        }

        let mut ping_tracker_result = None;
        if let Some(pong) = routing_label.pong {
            ping_tracker_result = Some(state.ping_tracker.got_pong(Instant::now(), pong));
        }

        if packet.len() == 0 {
            // Packet was just control bits
            return Ok(());
        }

        // state updates complete
        drop(state);

        ping_tracker_result.map(|r| self.handle_ping_tracker_result(r));

        if routing_label.dst == self.own_node_id {
            let peer = match routing_label.target {
                Endpoint::Server => {
                    let hdr = quiche::Header::from_slice(packet, quiche::MAX_CONN_ID_LEN)
                        .context("Decoding quic header")?;

                    if hdr.ty == quiche::Type::VersionNegotiation {
                        bail!("Version negotiation invalid on the server");
                    }

                    // If we're asked for a server connection, we should create a client connection
                    self.router
                        .ensure_client_peer(routing_label.src, &Rc::downgrade(self))
                        .await
                        .context("Handling server packet to self")?;

                    if let Some(server_peer) = self
                        .router
                        .server_peer(
                            routing_label.src,
                            hdr.ty == quiche::Type::Initial,
                            &Rc::downgrade(self),
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
                    .client_peer(routing_label.src, &Rc::downgrade(self))
                    .await
                    .context("Routing packet to own client")?,
            };
            peer.receive_frame(packet).context("Receiving packet on quic connection")?;
        } else {
            // If we're asked to forward a packet, we should have client connections to each end
            self.router
                .ensure_client_peer(routing_label.src, &Rc::downgrade(self))
                .await
                .context("Forwarding packet source summoning")?;
            // Just need to get the peer's current_link, doesn't matter server or client, so grab client because it's not optional
            let peer = self
                .router
                .client_peer(routing_label.dst, &Rc::downgrade(self))
                .await
                .context("Routing packet to other client")?;
            if let Some(link) = peer.current_link().await {
                link.forward(routing_label, packet)?;
            }
        }

        Ok(())
    }

    fn forward(&self, routing_label: RoutingLabel, frame: &mut [u8]) -> Result<(), Error> {
        let mut state = self.state.borrow_mut();
        state.packets_forwarded += 1;
        state.to_forward.push_back((routing_label, frame.to_vec()));
        state.wake_send();
        Ok(())
    }

    /// Fetch the next frame that should be sent by the link. Returns Ok(None) on link
    /// closure, Ok(Some(packet_length)) on successful read, and an error otherwise.
    pub async fn next_send(self: &Rc<Link>, frame: &mut [u8]) -> Result<Option<usize>, Error> {
        loop {
            match (NextLinkSend { link: self, frame }).await {
                Ok(Some((n, ping_tracker_result))) => {
                    self.handle_ping_tracker_result(ping_tracker_result).await;
                    if n != 0 {
                        return Ok(Some(n));
                    }
                }
                Ok(None) => return Ok(None),
                Err(e) => return Err(e),
            }
        }
    }

    /// Return an `Observer` against this links description
    pub(crate) fn new_description_observer(&self) -> Observer<Option<LinkDescription>> {
        self.description.new_observer()
    }

    async fn handle_ping_tracker_result(self: &Rc<Link>, r: PingTrackerResult) {
        let mut state = self.state.borrow_mut();
        if r.sched_send {
            state.wake_send();
        }
        if r.new_round_trip_time {
            // Trigger a send of link state to peers.
            self.router.schedule_link_status_update();
            let desc = state.make_desc();
            self.description.push(desc);
        }
        if let Some(dt) = r.sched_timeout {
            // TODO: keep a dedicated thing around to do this, cancel previous timeouts
            state.timeout_generation += 1;
            let gen = state.timeout_generation;
            drop(state);
            let at = std::time::Instant::now() + dt;
            let link = self.clone();
            spawn(async move {
                wait_until(at).await;
                let mut state = link.state.borrow_mut();
                if gen == state.timeout_generation {
                    let r = state.ping_tracker.on_timeout(std::time::Instant::now());
                    drop(state);
                    link.handle_ping_tracker_result(r).await;
                }
            });
        }
    }

    pub(crate) fn make_status(&self) -> Option<LinkStatus> {
        self.state.borrow().make_desc().map(|desc| {
            let round_trip_time = desc.round_trip_time.as_micros();
            LinkStatus {
                local_id: self.node_link_id,
                to: self.peer_node_id,
                round_trip_time: if round_trip_time > std::u64::MAX as u128 {
                    std::u64::MAX
                } else {
                    round_trip_time as u64
                },
            }
        })
    }
}

#[derive(Clone)]
pub struct LinkStatus {
    local_id: NodeLinkId,
    to: NodeId,
    round_trip_time: u64,
}

impl From<LinkStatus> for fidl_fuchsia_overnet_protocol::LinkStatus {
    fn from(status: LinkStatus) -> fidl_fuchsia_overnet_protocol::LinkStatus {
        fidl_fuchsia_overnet_protocol::LinkStatus {
            local_id: status.local_id.0,
            to: status.to.into(),
            metrics: LinkMetrics { round_trip_time: Some(status.round_trip_time) },
        }
    }
}

impl LinkState {
    fn make_desc(&self) -> Option<LinkDescription> {
        self.ping_tracker
            .round_trip_time()
            .map(|round_trip_time| LinkDescription { round_trip_time })
    }

    fn wake_send(&mut self) {
        self.waiting_for_send.take().map(|w| w.wake());
    }
}

pub struct NextLinkSend<'a> {
    link: &'a Rc<Link>,
    frame: &'a mut [u8],
}

impl<'a> NextLinkSend<'a> {
    fn poll_next_peer_packet(
        &mut self,
        ctx: &mut Context<'_>,
    ) -> Poll<Result<Option<(usize, NodeId, NodeId, Endpoint)>, Error>> {
        let frame: &mut [u8] = self.frame;
        let frame_len = frame.len();
        let frame = &mut frame[..frame_len - MAX_ROUTING_LABEL_LENGTH];
        drop(frame_len);
        let state = &mut self.link.state.borrow_mut();
        if let Some((routing_label, forward_frame)) = state.to_forward.pop_front() {
            let n = forward_frame.len();
            if n > frame.len() {
                return Poll::Ready(Err(format_err!("Forwarded packet too long to forward")));
            }
            frame[..n].clone_from_slice(&forward_frame);
            return Poll::Ready(Ok(Some((
                n,
                routing_label.src,
                routing_label.dst,
                routing_label.target,
            ))));
        }
        for peer in state.route_for_peers.iter() {
            if let Some(peer) = Weak::upgrade(peer) {
                match peer.poll_next_send(self.link.own_node_id, frame, ctx) {
                    Poll::Ready(Ok(Some(x))) => return Poll::Ready(Ok(Some(x))),
                    Poll::Ready(Ok(None)) => (),
                    Poll::Ready(Err(e)) => log::warn!("Error getting next packet: {:?}", e),
                    Poll::Pending => (),
                }
            }
        }
        state.waiting_for_send = Some(ctx.waker().clone());
        Poll::Pending
    }
}

impl<'a> Future for NextLinkSend<'a> {
    type Output = Result<Option<(usize, PingTrackerResult)>, Error>;

    fn poll(self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Self::Output> {
        let inner = Pin::into_inner(self);
        match inner.poll_next_peer_packet(ctx) {
            Poll::Ready(Ok(None)) => Poll::Ready(Ok(None)),
            Poll::Ready(Err(e)) => Poll::Ready(Err(e)),
            Poll::Ready(Ok(Some((n, src, dst, target)))) => {
                let frame = &mut inner.frame;
                let mut state = inner.link.state.borrow_mut();
                let (ping, ping_tracker_result) =
                    state.ping_tracker.maybe_send_ping(Instant::now(), false);
                let pong = state.pong_tracker.maybe_send_pong();
                if ping.is_some() {
                    state.pings_sent += 1;
                }
                let rl = RoutingLabel {
                    src,
                    dst,
                    ping,
                    pong,
                    target,
                    debug_token: RoutingLabel::new_debug_token(),
                };
                log::trace!("outgoing routing label {:?}", rl);
                let suffix_len = rl
                    .encode_for_link(
                        inner.link.own_node_id,
                        inner.link.peer_node_id,
                        &mut frame[n..],
                    )
                    .context("Formatting regular packet for send")?;
                let packet_len = n + suffix_len;
                state.sent_packets += 1;
                state.sent_bytes += packet_len as u64;
                Poll::Ready(Ok(Some((packet_len, ping_tracker_result))))
            }
            Poll::Pending => {
                // No packet from a link, but we should check if we need to ping/pong
                let frame = &mut inner.frame;
                let mut state = inner.link.state.borrow_mut();
                let (ping, ping_tracker_result) =
                    state.ping_tracker.maybe_send_ping(Instant::now(), true);
                let pong = state.pong_tracker.maybe_send_pong();
                if ping.is_some() {
                    state.pings_sent += 1;
                }
                if ping.is_some() || pong.is_some() {
                    let len = RoutingLabel {
                        src: inner.link.own_node_id,
                        dst: inner.link.peer_node_id,
                        ping,
                        pong,
                        target: Endpoint::Client,
                        debug_token: RoutingLabel::new_debug_token(),
                    }
                    .encode_for_link(inner.link.own_node_id, inner.link.peer_node_id, frame)
                    .context("Formatting control packet for send")?;
                    state.sent_packets += 1;
                    state.sent_bytes += len as u64;
                    Poll::Ready(Ok(Some((len, ping_tracker_result))))
                } else {
                    if ping_tracker_result.anything_to_do() {
                        Poll::Ready(Ok(Some((0, ping_tracker_result))))
                    } else {
                        Poll::Pending
                    }
                }
            }
        }
    }
}
