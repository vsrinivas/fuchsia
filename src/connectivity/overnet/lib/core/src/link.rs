// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A `Link` describes an established communications channel between two nodes.

use crate::{
    coding::{decode_fidl, encode_fidl},
    future_help::{log_errors, Observer, SelectThenNowOrNever},
    labels::{NodeId, NodeLinkId},
    link_frame_label::{FrameType, LinkFrameLabel, RoutingTarget, LINK_FRAME_LABEL_MAX_SIZE},
    ping_tracker::{PingSender, PingTracker},
    router::Router,
    routes::ForwardingTable,
};
use anyhow::{bail, format_err, Context as _, Error};
use cutex::{AcquisitionPredicate, Cutex, CutexGuard, CutexTicket};
use fidl_fuchsia_overnet_protocol::{
    LinkControlFrame, LinkControlMessage, LinkControlPayload, LinkDiagnosticInfo, Route, SetRoute,
};
use fuchsia_async::{Task, Timer};
use futures::{future::Either, lock::Mutex, pin_mut, prelude::*};
use std::{
    convert::TryInto,
    pin::Pin,
    sync::atomic::{AtomicU64, Ordering},
    sync::Arc,
    time::Duration,
};

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

/// Maximum length of a frame that can be sent over a link.
pub(crate) const MAX_FRAME_LENGTH: usize = 1400;
/// Maximum payload length that's encodable (allowing frame space for labelling from the link).
const MAX_PAYLOAD_LENGTH: usize = MAX_FRAME_LENGTH - LINK_FRAME_LABEL_MAX_SIZE;
/// Maximum length of a SetRoute payload
const MAX_SET_ROUTE_LENGTH: usize = MAX_PAYLOAD_LENGTH - 64;
/// Maximum number of frames queued in OutputQueue
const MAX_QUEUED_FRAMES: usize = 32;

#[derive(Debug)]
struct OutputFrame {
    frame_type: FrameType,
    target: RoutingTarget,
    length: usize,
    bytes: [u8; MAX_FRAME_LENGTH],
}

impl Default for OutputFrame {
    fn default() -> OutputFrame {
        OutputFrame {
            frame_type: FrameType::Message,
            target: RoutingTarget { dst: 0.into(), src: 0.into() },
            bytes: [0u8; MAX_FRAME_LENGTH],
            length: 0,
        }
    }
}

/// Staging area for emitting frames - forms a short queue of pending frames and
/// control structure around that.
pub(crate) struct OutputQueue {
    // Links peer - here to assert no loop sends.
    peer_node_id: NodeId,
    /// Is the link open?
    open: bool,
    /// Control frame emitted sequence number.
    control_sent_seq: u64,
    /// Control frame acked sequence number - we can send when acked == sent.
    control_acked_seq: u64,
    /// Routing labels for emission.
    frames: [OutputFrame; MAX_QUEUED_FRAMES],
    /// First frame to output.
    first_frame: usize,
    /// Number of frames queued.
    num_frames: usize,
}

impl std::fmt::Debug for OutputQueue {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        #[derive(Debug)]
        struct OutputFrameView {
            frame_type: FrameType,
            target: RoutingTarget,
            length: usize,
        };
        let mut frames = Vec::new();
        for i in self.first_frame..self.first_frame + self.num_frames {
            let frame = &self.frames[i % MAX_QUEUED_FRAMES];
            frames.push(OutputFrameView {
                frame_type: frame.frame_type,
                target: frame.target,
                length: frame.length,
            });
        }
        f.debug_struct("OutputQueue")
            .field("control_sent_seq", &self.control_sent_seq)
            .field("control_acked_seq", &self.control_acked_seq)
            .field("frames", &frames)
            .finish()
    }
}

impl OutputQueue {
    /// Update state machine to note readiness to send.
    pub fn send<'a>(
        &'a mut self,
        target: RoutingTarget,
        frame_type: FrameType,
    ) -> Result<PartialSend<'a>, Error> {
        if !self.open {
            return Err(format_err!("link closed"));
        }
        assert_ne!(self.peer_node_id, target.src);
        assert!(self.num_frames < MAX_QUEUED_FRAMES);
        let frame = (self.first_frame + self.num_frames) % MAX_QUEUED_FRAMES;
        let output_frame = &mut self.frames[frame];
        output_frame.target = target;
        output_frame.frame_type = frame_type;
        output_frame.length = 0;
        Ok(PartialSend { output_frame, num_frames: &mut self.num_frames })
    }
}

#[must_use]
pub(crate) struct PartialSend<'a> {
    output_frame: &'a mut OutputFrame,
    num_frames: &'a mut usize,
}

impl<'a> PartialSend<'a> {
    pub fn buffer(&mut self) -> &mut [u8] {
        &mut self.output_frame.bytes[..MAX_PAYLOAD_LENGTH]
    }

    pub fn commit(self, length: usize) {
        assert!(length <= MAX_PAYLOAD_LENGTH);
        self.output_frame.length = length;
        *self.num_frames += 1;
    }

    pub fn commit_copy(mut self, buffer: &[u8]) -> Result<(), Error> {
        if buffer.len() > MAX_PAYLOAD_LENGTH {
            return Err(format_err!(
                "message too long: {}b vs {}b",
                buffer.len(),
                MAX_PAYLOAD_LENGTH
            ));
        }
        self.buffer()[..buffer.len()].copy_from_slice(buffer);
        self.commit(buffer.len());
        Ok(())
    }
}

/// Cutex predicate to wait until no packet is queued to send before acquisition of the cutex.
struct ReadyToSend;
impl AcquisitionPredicate<OutputQueue> for ReadyToSend {
    fn can_lock(&self, output_queue: &OutputQueue) -> bool {
        !output_queue.open || output_queue.num_frames < MAX_QUEUED_FRAMES
    }

    fn debug(&self, fmt: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        fmt.write_str("ready-to-send")
    }
}
const READY_TO_SEND: ReadyToSend = ReadyToSend;

/// Cutex predicate to wait until no packet is queued to send AND existing control packets have
/// been acked before acquisition of the cutex.
struct ReadyToSendNewControl;
impl AcquisitionPredicate<OutputQueue> for ReadyToSendNewControl {
    fn can_lock(&self, output_queue: &OutputQueue) -> bool {
        if !output_queue.open {
            return true;
        }
        if output_queue.num_frames == MAX_QUEUED_FRAMES {
            return false;
        }
        return output_queue.control_sent_seq == output_queue.control_acked_seq;
    }

    fn debug(&self, fmt: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        fmt.write_str("ready-to-send-new-control")
    }
}
const READY_TO_SEND_NEW_CONTROL: ReadyToSendNewControl = ReadyToSendNewControl;

/// Cutex predicate to wait until a particular control message sequence number is acknowledged.
struct AckedControlSeq(u64);
impl AcquisitionPredicate<OutputQueue> for AckedControlSeq {
    fn can_lock(&self, output_queue: &OutputQueue) -> bool {
        output_queue.control_acked_seq >= self.0
    }

    fn debug(&self, fmt: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(fmt, "acked-control-seq({})", self.0)
    }
}

/// A frame that ought to be sent by a link implementation.
/// Potentially holds an interior lock to the underlying link, so this should be released before
/// calling into another link.
pub struct SendFrame<'a>(SendFrameInner<'a>);

enum SendFrameInner<'a> {
    /// Send the frame that's been queued up in the frame output.
    FromFrameOutput(CutexGuard<'a, OutputQueue>, usize),
    /// Send these exact bytes.
    /// Right now this is only needed for frame labels without payloads (specifically pings).
    /// Consequently we restrict the length to just what is needed for those.
    Raw { bytes: [u8; LINK_FRAME_LABEL_MAX_SIZE], length: usize },
}

impl<'a> SendFrame<'a> {
    pub fn bytes(&self) -> &[u8] {
        match &self.0 {
            SendFrameInner::FromFrameOutput(g, frame) => {
                let frame = &g.frames[*frame];
                &frame.bytes[..frame.length]
            }
            SendFrameInner::Raw { bytes, length } => &bytes[..*length],
        }
    }
}

/// Creates a link configuration description for diagnostics services.
pub type ConfigProducer =
    Box<dyn Send + Sync + Fn() -> Option<fidl_fuchsia_overnet_protocol::LinkConfig>>;

/// Sender for a link
pub struct LinkSender {
    runner: Arc<LinkRunner>,
    routing: Arc<LinkRouting>,
}

/// Receiver for a link
pub struct LinkReceiver {
    runner: Arc<LinkRunner>,
    routing: Arc<LinkRouting>,
    forwarding_table: Arc<Mutex<ForwardingTable>>,
    route_updates: Vec<Route>,
    received_seq: u64,
}

pub(crate) fn new_link(
    peer_node_id: NodeId,
    node_link_id: NodeLinkId,
    router: &Arc<Router>,
    config: ConfigProducer,
) -> (LinkSender, LinkReceiver, Arc<LinkRouting>, Observer<Option<Duration>>) {
    let (ping_tracker, observer1, observer2) = PingTracker::new();
    let routing = Arc::new(LinkRouting {
        own_node_id: router.node_id(),
        peer_node_id,
        node_link_id,
        rtt_observer: Mutex::new(observer2),
        output: Cutex::new(OutputQueue {
            peer_node_id,
            control_acked_seq: 0,
            control_sent_seq: 0,
            frames: Default::default(),
            first_frame: 0,
            num_frames: 0,
            open: true,
        }),
        stats: LinkStats {
            packets_forwarded: AtomicU64::new(0),
            pings_sent: AtomicU64::new(0),
            received_bytes: AtomicU64::new(0),
            sent_bytes: AtomicU64::new(0),
            received_packets: AtomicU64::new(0),
            sent_packets: AtomicU64::new(0),
        },
        config,
    });
    let forwarding_table = Arc::new(Mutex::new(ForwardingTable::empty()));
    let run_loop = log_errors(
        futures::future::try_join(
            ping_tracker.run(),
            send_state(
                routing.clone(),
                router.new_forwarding_table_observer(),
                forwarding_table.clone(),
            ),
        )
        .map_ok(drop),
        format_err!("link {:?} run_loop failed", routing.debug_id()),
    );
    let runner =
        Arc::new(LinkRunner { ping_tracker, router: router.clone(), _task: Task::spawn(run_loop) });
    (
        LinkSender { routing: routing.clone(), runner: runner.clone() },
        LinkReceiver {
            routing: routing.clone(),
            runner,
            forwarding_table,
            route_updates: Vec::new(),
            received_seq: 0,
        },
        routing,
        observer1,
    )
}

/// Background task that watches for forwarding table updates from the router and sends them
/// to this links peer.
async fn send_state(
    routing: Arc<LinkRouting>,
    mut forwarding_table: Observer<ForwardingTable>,
    forwarding_forwarding_table: Arc<Mutex<ForwardingTable>>,
) -> Result<(), Error> {
    let mut last_emitted = ForwardingTable::empty();
    while let Some(forwarding_table) = forwarding_table.next().await {
        *forwarding_forwarding_table.lock().await = forwarding_table.clone();
        // Remove any routes that would cause a loop to form.
        let forwarding_table = forwarding_table.filter_out_via(routing.peer_node_id);
        // Only send an update if the delta is 'significant' -- either routes have changed,
        // or metrics have changed so significantly that downstream routes are likely to need
        // to be updated (this is a heuristic).
        if forwarding_table.is_significantly_different_to(&last_emitted) {
            log::trace!(
                "[{:?}] Send new forwarding table: {:?}",
                routing.debug_id(),
                forwarding_table
            );
            let empty_output = || SetRoute { is_end: false, routes: Vec::new() };
            let mut output = empty_output();
            for (destination, metrics) in forwarding_table.iter() {
                output
                    .routes
                    .push(Route { destination: destination.into(), route_metrics: metrics.into() });
                if encode_fidl(&mut output)?.len() > MAX_SET_ROUTE_LENGTH {
                    let route = output.routes.pop().unwrap();
                    routing
                        .send_control_message(LinkControlPayload::SetRoute(std::mem::replace(
                            &mut output,
                            empty_output(),
                        )))
                        .await?;
                    output.routes.push(route);
                    assert!(encode_fidl(&mut output)?.len() <= MAX_SET_ROUTE_LENGTH);
                }
            }
            output.is_end = true;
            routing.send_control_message(LinkControlPayload::SetRoute(output)).await?;
            last_emitted = forwarding_table;
        }
    }
    Ok(())
}

/// Routing data gor a link
pub(crate) struct LinkRouting {
    own_node_id: NodeId,
    peer_node_id: NodeId,
    node_link_id: NodeLinkId,
    output: Cutex<OutputQueue>,
    stats: LinkStats,
    rtt_observer: Mutex<Observer<Option<Duration>>>,
    config: ConfigProducer,
}

impl std::fmt::Debug for LinkRouting {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "LINK({:?}:{:?}->{:?})", self.node_link_id, self.own_node_id, self.peer_node_id)
    }
}

impl LinkRouting {
    pub(crate) fn id(&self) -> NodeLinkId {
        self.node_link_id
    }

    pub(crate) fn own_node_id(&self) -> NodeId {
        self.own_node_id
    }

    pub(crate) fn debug_id(&self) -> impl std::fmt::Debug {
        (self.node_link_id, self.own_node_id, self.peer_node_id)
    }

    /// Construct a routing target to send to this links peer from this link.
    pub(crate) fn peer_target(&self) -> RoutingTarget {
        RoutingTarget { dst: self.peer_node_id, src: self.own_node_id }
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
            config: (self.config)(),
            ..LinkDiagnosticInfo::empty()
        }
    }

    /// Produce a ticket to acquire a cutex once it becomes sendable.
    pub(crate) fn new_send_ticket<'a>(&'a self) -> CutexTicket<'a, 'static, OutputQueue> {
        CutexTicket::new_when_pinned(&self.output, Pin::new(&READY_TO_SEND))
    }

    pub(crate) async fn is_closed(&self) -> bool {
        !self.output.lock().await.open
    }

    async fn close(&self) {
        log::trace!("CLOSE LINK: {:?}", self.debug_id());
        self.output.lock().await.open = false;
    }

    fn close_in_background(self: Arc<Self>) {
        Task::spawn(async move { self.close().await }).detach()
    }

    /// Send a control message to our peer with some payload.
    /// Implements periodic resends until an ack is received.
    async fn send_control_message(&self, payload: LinkControlPayload) -> Result<(), Error> {
        let mut output = self.output.lock_when_pinned(Pin::new(&READY_TO_SEND_NEW_CONTROL)).await;
        let seq = output.control_sent_seq + 1;
        let mut frame = LinkControlFrame::Message(LinkControlMessage { seq, payload });
        let message = encode_fidl(&mut frame)?;
        log::trace!("[{:?}] send control frame {:?}", self.debug_id(), frame);
        output.send(self.peer_target(), FrameType::Control)?.commit_copy(&message)?;
        output.control_sent_seq = seq;
        drop(output);

        // Resend periodically until acked.
        let done_predicate = AckedControlSeq(seq);
        pin_mut!(done_predicate);
        loop {
            let rtt = self
                .rtt_observer
                .lock()
                .await
                .peek()
                .await
                .flatten()
                .unwrap_or(Duration::from_secs(1));
            match futures::future::select(
                self.output.lock_when_pinned(done_predicate.as_ref()),
                Timer::new(8 * rtt),
            )
            .await
            {
                Either::Left(_) => break,
                Either::Right((_, lock)) => {
                    drop(lock);
                    let mut output = self.output.lock_when_pinned(Pin::new(&READY_TO_SEND)).await;
                    if output.control_acked_seq >= seq {
                        break;
                    }
                    log::trace!(
                        "[{:?}] re-send control frame {:?} [rtt={:?}]",
                        self.debug_id(),
                        frame,
                        rtt
                    );
                    output.send(self.peer_target(), FrameType::Control)?.commit_copy(&message)?;
                }
            }
        }

        Ok(())
    }
}

impl Drop for LinkReceiver {
    fn drop(&mut self) {
        self.routing.clone().close_in_background()
    }
}

impl LinkReceiver {
    /// Returns some moniker identifying the underlying link, for use in debugging.
    pub fn debug_id(&self) -> impl std::fmt::Debug {
        self.routing.debug_id()
    }

    /// Remove the label from a frame.
    async fn remove_label<'a>(
        &mut self,
        frame: &'a mut [u8],
    ) -> Result<Option<(LinkFrameLabel, &'a mut [u8])>, Error> {
        let runner = &*self.runner;
        let routing = &*self.routing;

        routing.stats.received_packets.fetch_add(1, Ordering::Relaxed);
        routing.stats.received_bytes.fetch_add(frame.len() as u64, Ordering::Relaxed);
        if frame.len() < 1 {
            bail!("Received empty frame");
        }
        let (routing_label, frame_length) =
            LinkFrameLabel::decode(routing.peer_node_id, routing.own_node_id, frame).with_context(
                || {
                    format_err!(
                        "Decoding routing label because {:?} received frame from {:?}",
                        routing.own_node_id,
                        routing.peer_node_id,
                    )
                },
            )?;
        let frame = &mut frame[..frame_length];
        if routing_label.target.src == routing.own_node_id {
            // Got a frame that was sourced here: break the infinite loop by definitely not
            // processing it.
            bail!(
                "[{:?}] Received looped frame; routing_label={:?} frame_length={}",
                routing.debug_id(),
                routing_label,
                frame_length
            );
        }
        if let Some(ping) = routing_label.ping {
            runner.ping_tracker.got_ping(ping).await;
        }
        if let Some(pong) = routing_label.pong {
            runner.ping_tracker.got_pong(pong).await;
        }
        if frame.len() == 0 {
            // Packet was just control bits
            return Ok(None);
        }

        Ok(Some((routing_label, frame)))
    }

    async fn handle_control(
        &mut self,
        routing_label: LinkFrameLabel,
        frame: &mut [u8],
    ) -> Result<(), Error> {
        let runner = &*self.runner;
        let routing = &*self.routing;

        if routing_label.target.dst != routing.own_node_id
            || routing_label.target.src != routing.peer_node_id
        {
            bail!(
                "[{:?}] Received routed control frame; routing_label={:?}",
                routing.debug_id(),
                routing_label
            );
        }
        let frame = decode_fidl(frame)?;
        log::trace!("[{:?}] Received control frame {:?}", routing.debug_id(), frame);
        match frame {
            LinkControlFrame::Ack(seq) => {
                let mut frame_output = self.routing.output.lock().await;
                if seq == frame_output.control_sent_seq {
                    frame_output.control_acked_seq = seq;
                }
            }
            LinkControlFrame::Message(LinkControlMessage { seq, payload }) => {
                if seq == self.received_seq {
                    // Ignore but fall through to ack code below.
                } else if seq == self.received_seq + 1 {
                    match payload {
                        LinkControlPayload::SetRoute(SetRoute { routes, is_end }) => {
                            self.route_updates.extend_from_slice(&*routes);
                            if is_end {
                                runner
                                    .router
                                    .update_routes(
                                        routing.peer_node_id,
                                        std::mem::take(&mut self.route_updates).into_iter().map(
                                            |Route { destination, route_metrics }| {
                                                (destination.into(), route_metrics)
                                            },
                                        ),
                                    )
                                    .await
                            }
                        }
                    }
                } else if seq > self.received_seq {
                    bail!(
                        "[{:?}] saw future message seq={} but we are at {}",
                        routing.debug_id(),
                        seq,
                        self.received_seq
                    );
                } else {
                    bail!(
                        "[{:?}] saw ancient message seq={} but we are at {}",
                        routing.debug_id(),
                        seq,
                        self.received_seq
                    );
                }
                self.received_seq = seq;
                let ack = encode_fidl(&mut LinkControlFrame::Ack(seq))?;
                self.routing
                    .output
                    .lock_when_pinned(Pin::new(&READY_TO_SEND))
                    .await
                    .send(routing.peer_target(), FrameType::Control)?
                    .commit_copy(&ack)?;
            }
        }
        Ok(())
    }

    async fn handle_message(
        &mut self,
        routing_label: LinkFrameLabel,
        frame: &mut [u8],
    ) -> Result<(), Error> {
        let runner = &*self.runner;
        let routing = &*self.routing;

        let hdr =
            quiche::Header::from_slice(frame, quiche::MAX_CONN_ID_LEN).with_context(|| {
                format!(
                    "Decoding quic header; link={:?}; routing_label={:?}",
                    routing.debug_id(),
                    routing_label
                )
            })?;
        let peer = runner
            .router
            .lookup_peer(&hdr.dcid, hdr.ty, routing_label.target.src)
            .await
            .with_context(|| {
                format!(
                    "link={:?}; routing_label={:?}; hdr={:?}",
                    routing.debug_id(),
                    routing_label,
                    hdr
                )
            })?;
        peer.receive_frame(frame).await.with_context(|| {
            format!(
                concat!(
                    "Receiving frame on quic connection;",
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
        Ok(())
    }

    async fn forward_message(
        &mut self,
        routing_label: LinkFrameLabel,
        frame: &mut [u8],
    ) -> Result<(), Error> {
        let runner = &*self.runner;
        let routing = &*self.routing;

        if let Some(via) = self.forwarding_table.lock().await.route_for(routing_label.target.dst) {
            log::trace!("[{:?}] fwd {:?} via {:?}", self.debug_id(), routing_label, via);
            if let Some(via) = runner.router.get_link(via).await {
                if via.node_link_id == routing.node_link_id
                    || via.peer_node_id == routing_label.target.src
                {
                    // This is a looped frame - signal to the sender to avoid this and drop it
                    log::trace!("[{:?}] Dropping frame due to routing loop", self.debug_id());
                    return Ok(());
                }
                via.output
                    .lock_when_pinned(Pin::new(&READY_TO_SEND))
                    .await
                    .send(routing_label.target, FrameType::Message)?
                    .commit_copy(frame)?;
            } else {
                log::trace!("[{:?}] Dropping frame because no via", self.debug_id());
            }
        } else {
            log::trace!("Drop forwarded packet {:?} - no route to dest", routing_label);
        }
        Ok(())
    }

    /// Report a frame was received.
    /// An error processing a frame does not indicate that the link should be closed.
    /// TODO: rename to received_frame for consistency.
    pub async fn received_packet(&mut self, frame: &mut [u8]) -> Result<(), Error> {
        if let Some((routing_label, frame)) = self.remove_label(frame).await? {
            return match (
                routing_label.frame_type,
                routing_label.target.dst == self.routing.own_node_id,
            ) {
                (FrameType::Control, _) => self.handle_control(routing_label, frame).await,
                (FrameType::Message, true) => self.handle_message(routing_label, frame).await,
                (FrameType::Message, false) => self.forward_message(routing_label, frame).await,
            };
        }
        Ok(())
    }
}

impl Drop for LinkSender {
    fn drop(&mut self) {
        self.routing.clone().close_in_background()
    }
}

impl LinkSender {
    pub(crate) fn router(&self) -> &Arc<Router> {
        &self.runner.router
    }

    /// Returns some moniker identifying the underlying link, for use in debugging.
    pub fn debug_id(&self) -> impl std::fmt::Debug {
        self.routing.debug_id()
    }

    /// Retrieve the next frame that should be sent via this link.
    /// Returns: Some(p) to send a packet `p`
    ///          None to indicate link closure
    pub async fn next_send(&self) -> Option<SendFrame<'_>> {
        let runner = &self.runner;
        let routing = &self.routing;
        let lock_send = self.routing.output.lock_when(|output_queue: &OutputQueue| {
            !output_queue.open || output_queue.num_frames > 0
        });
        let ping_pong = PingSender::new(&runner.ping_tracker);
        let (output_queue, ping_pong) = SelectThenNowOrNever::new(lock_send, ping_pong).await;
        let (ping, pong) = match ping_pong {
            None => (None, None),
            Some((ping, pong)) => (ping, pong),
        };
        if ping.is_some() {
            routing.stats.pings_sent.fetch_add(1, Ordering::Relaxed);
        }
        if let Some(mut output_queue) = output_queue {
            if !output_queue.open {
                None
            } else {
                assert_ne!(output_queue.num_frames, 0);
                let frame_index = output_queue.first_frame;
                let frame = &mut output_queue.frames[frame_index];
                let target = frame.target;
                let frame_type = frame.frame_type;
                assert_ne!(target.src, routing.peer_node_id);
                let label = LinkFrameLabel {
                    target,
                    ping,
                    pong,
                    frame_type,
                    debug_token: LinkFrameLabel::new_debug_token(),
                };
                let original_length = frame.length;
                let n_tail = label
                    .encode_for_link(
                        routing.own_node_id,
                        routing.peer_node_id,
                        &mut frame.bytes[original_length..],
                    )
                    .expect("encode_for_link should always succeed");
                frame.length += n_tail;
                routing.stats.sent_packets.fetch_add(1, Ordering::Relaxed);
                routing.stats.sent_bytes.fetch_add(frame.length as u64, Ordering::Relaxed);
                output_queue.first_frame = (output_queue.first_frame + 1) % MAX_QUEUED_FRAMES;
                output_queue.num_frames -= 1;
                // SendFrame continues to hold the OutputQueue cutex.
                Some(SendFrame(SendFrameInner::FromFrameOutput(output_queue, frame_index)))
            }
        } else {
            assert!(ping.is_some() || pong.is_some());
            let label = LinkFrameLabel {
                target: RoutingTarget { src: routing.own_node_id, dst: routing.peer_node_id },
                ping,
                pong,
                frame_type: FrameType::Message,
                debug_token: LinkFrameLabel::new_debug_token(),
            };
            log::trace!("link {:?} deliver {:?}", routing.debug_id(), label);
            let mut bytes = [0u8; LINK_FRAME_LABEL_MAX_SIZE];
            let length = label
                .encode_for_link(routing.own_node_id, routing.peer_node_id, &mut bytes[..])
                .expect("encode_for_link should always succeed");
            routing.stats.sent_packets.fetch_add(1, Ordering::Relaxed);
            routing.stats.sent_bytes.fetch_add(length as u64, Ordering::Relaxed);
            Some(SendFrame(SendFrameInner::Raw { bytes, length }))
        }
    }
}
