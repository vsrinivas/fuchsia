// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A `Link` describes an established communications channel between two nodes.

mod frame_label;
mod ping_tracker;

use self::{
    frame_label::{LinkFrameLabel, LINK_FRAME_LABEL_MAX_SIZE},
    ping_tracker::PingTracker,
};
use crate::{
    coding::{self, decode_fidl_with_context, encode_fidl_with_context},
    future_help::{log_errors, Observable, Observer},
    labels::{NodeId, NodeLinkId},
    router::{AscenddClientRouting, ConnectingLinkToken, ForwardingTable, Router},
};
use anyhow::{bail, format_err, Context as _, Error};
use cutex::{AcquisitionPredicate, Cutex, CutexGuard, CutexTicket};
use fidl_fuchsia_net::{
    Ipv4Address, Ipv4SocketAddress, Ipv6Address, Ipv6SocketAddress, SocketAddress,
};
use fidl_fuchsia_overnet_protocol::{
    LinkConfig, LinkControlFrame, LinkControlMessage, LinkControlPayload, LinkDiagnosticInfo,
    LinkIntroduction, Route, SetRoute,
};
use fuchsia_async::{Task, TimeoutExt, Timer};
use futures::{
    channel::{mpsc, oneshot},
    future::Either,
    lock::Mutex,
    pin_mut,
    prelude::*,
};
use rand::Rng;
use std::{
    convert::TryInto,
    net::{Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV4, SocketAddrV6},
    num::NonZeroU64,
    pin::Pin,
    sync::atomic::{AtomicU64, Ordering},
    sync::{Arc, Weak},
    time::Duration,
};

pub use self::frame_label::{RoutingDestination, RoutingTarget};

struct LinkStats {
    packets_forwarded: AtomicU64,
    pings_sent: AtomicU64,
    received_bytes: AtomicU64,
    sent_bytes: AtomicU64,
    received_packets: AtomicU64,
    sent_packets: AtomicU64,
}

fn convert_ipv6_buffer(in_arr: [u8; 16]) -> [u16; 8] {
    let mut out_arr: [u16; 8] = [0; 8];

    for i in 0..8 {
        out_arr[i] = ((in_arr[2 * i] as u16) << 8) | (in_arr[2 * i + 1] as u16);
    }

    out_arr
}

/// Metadata to be sent during the link introduction
#[derive(Default, Debug)]
pub struct LinkIntroductionFacts {
    /// The socket address of this our peer as observed by us
    pub you_are: Option<SocketAddr>,
}

impl LinkIntroductionFacts {
    fn into_message(&self) -> LinkIntroduction {
        LinkIntroduction {
            you_are: self.you_are.map(|a| match a {
                SocketAddr::V4(v4) => SocketAddress::Ipv4(Ipv4SocketAddress {
                    address: Ipv4Address { addr: v4.ip().octets() },
                    port: v4.port(),
                }),
                SocketAddr::V6(v6) => SocketAddress::Ipv6(Ipv6SocketAddress {
                    address: Ipv6Address { addr: v6.ip().octets() },
                    port: v6.port(),
                    zone_index: v6.scope_id() as u64,
                }),
            }),
            ..LinkIntroduction::EMPTY
        }
    }

    fn from_message(introduction: LinkIntroduction) -> Result<Self, Error> {
        Ok(LinkIntroductionFacts {
            you_are: introduction
                .you_are
                .map(|a| {
                    Ok::<_, Error>(match a {
                        SocketAddress::Ipv4(v4) => SocketAddr::V4(SocketAddrV4::new(
                            Ipv4Addr::new(
                                v4.address.addr[0],
                                v4.address.addr[1],
                                v4.address.addr[2],
                                v4.address.addr[3],
                            ),
                            v4.port,
                        )),
                        SocketAddress::Ipv6(v6) => {
                            let addr = convert_ipv6_buffer(v6.address.addr);
                            SocketAddr::V6(SocketAddrV6::new(
                                Ipv6Addr::new(
                                    addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6],
                                    addr[7],
                                ),
                                v6.port,
                                0,
                                v6.zone_index
                                    .try_into()
                                    .map_err(|_| format_err!("zone_index too large"))?,
                            ))
                        }
                    })
                })
                .transpose()?,
        })
    }
}

/// Maximum length of a frame that can be sent over a link.
pub const MAX_FRAME_LENGTH: usize = 1400;
/// Maximum payload length that's encodable (allowing frame space for labelling from the link).
const MAX_PAYLOAD_LENGTH: usize = MAX_FRAME_LENGTH - LINK_FRAME_LABEL_MAX_SIZE;
/// Maximum length of a SetRoute payload
const MAX_SET_ROUTE_LENGTH: usize = MAX_PAYLOAD_LENGTH - 64;
/// Maximum number of frames queued in OutputQueue
const MAX_QUEUED_FRAMES: usize = 32;

/// Maximum time to try and send a control message
const MAX_CONTROL_MESSAGE_RETRY_TIME: Duration = Duration::from_secs(30);
/// Maximum amount of time to wait before retrying a control message
const MAX_RESEND_DELAY: Duration = Duration::from_secs(1);
/// Minumum amount of time to wait before retrying a control message
const MIN_RESEND_DELAY: Duration = Duration::from_millis(1);

#[derive(Debug)]
struct OutputFrame {
    target: RoutingTarget,
    length: usize,
    bytes: [u8; MAX_FRAME_LENGTH],
}

impl Default for OutputFrame {
    fn default() -> OutputFrame {
        OutputFrame {
            target: RoutingTarget {
                src: 0.into(),
                dst: RoutingDestination::Control(coding::DEFAULT_CONTEXT),
            },
            bytes: [0u8; MAX_FRAME_LENGTH],
            length: 0,
        }
    }
}

/// Staging area for emitting frames - forms a short queue of pending frames and
/// control structure around that.
pub(crate) struct OutputQueue {
    // Links peer - here to assert no loop sends.
    peer_node_id: Option<NodeId>,
    /// Is the link open?
    open: bool,
    /// Has this link received an ack ever?
    received_any_ack: bool,
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
    /// Ping tracker
    ping_tracker: PingTracker,
}

impl std::fmt::Debug for OutputQueue {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        #[derive(Debug)]
        struct OutputFrameView {
            // TODO(fxbug.dev/84729)
            #[allow(unused)]
            target: RoutingTarget,
            // TODO(fxbug.dev/84729)
            #[allow(unused)]
            length: usize,
        }
        let mut frames = Vec::new();
        for i in self.first_frame..self.first_frame + self.num_frames {
            let frame = &self.frames[i % MAX_QUEUED_FRAMES];
            frames.push(OutputFrameView { target: frame.target, length: frame.length });
        }
        f.debug_struct("OutputQueue")
            .field("open", &self.open)
            .field("received_any_ack", &self.received_any_ack)
            .field("control_sent_seq", &self.control_sent_seq)
            .field("control_acked_seq", &self.control_acked_seq)
            .field("frames", &frames)
            .finish()
    }
}

impl OutputQueue {
    /// Update state machine to note readiness to send.
    pub fn send<'a>(&'a mut self, target: RoutingTarget) -> Result<PartialSend<'a>, Error> {
        if !self.open {
            return Err(format_err!("link closed"));
        }
        assert!(self.num_frames < MAX_QUEUED_FRAMES);
        let frame = (self.first_frame + self.num_frames) % MAX_QUEUED_FRAMES;
        let output_frame = &mut self.frames[frame];
        output_frame.target = target;
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

/// Cutex predicate to wait until no packet is queued to send and the link has become routable
/// before acquisition of the cutex.
struct ReadyToSendMessage;
impl AcquisitionPredicate<OutputQueue> for ReadyToSendMessage {
    fn can_lock(&self, output_queue: &OutputQueue) -> bool {
        !output_queue.open
            || (output_queue.num_frames < MAX_QUEUED_FRAMES && output_queue.peer_node_id.is_some())
    }

    fn debug(&self, fmt: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        fmt.write_str("ready-to-send")
    }
}
const READY_TO_SEND_MESSAGE: ReadyToSendMessage = ReadyToSendMessage;

/// Cutex predicate to wait until no packet is queued to send AND existing control packets have
/// been acked before acquisition of the cutex.
struct ReadyToSendNewControl;
impl AcquisitionPredicate<OutputQueue> for ReadyToSendNewControl {
    fn can_lock(&self, output_queue: &OutputQueue) -> bool {
        !output_queue.open
            || (output_queue.num_frames < MAX_QUEUED_FRAMES
                && output_queue.control_sent_seq == output_queue.control_acked_seq)
    }

    fn debug(&self, fmt: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        fmt.write_str("ready-to-send-new-control")
    }
}
const READY_TO_SEND_NEW_CONTROL: ReadyToSendNewControl = ReadyToSendNewControl;

/// Cutex predicate to wait until no packet is queued to send so we can resend a control message.
struct ReadyToResendControl;
impl AcquisitionPredicate<OutputQueue> for ReadyToResendControl {
    fn can_lock(&self, output_queue: &OutputQueue) -> bool {
        !output_queue.open || output_queue.num_frames < MAX_QUEUED_FRAMES
    }

    fn debug(&self, fmt: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        fmt.write_str("ready-to-resend-control")
    }
}
const READY_TO_RESEND_CONTROL: ReadyToResendControl = ReadyToResendControl;

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

/// Cutex predicate to wait until a peer node id is known.
struct HasPeerNodeId;
impl AcquisitionPredicate<OutputQueue> for HasPeerNodeId {
    fn can_lock(&self, output_queue: &OutputQueue) -> bool {
        output_queue.peer_node_id.is_some()
    }

    fn debug(&self, fmt: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        fmt.write_str("has-peer-node-id")
    }
}
const HAS_PEER_NODE_ID: HasPeerNodeId = HasPeerNodeId;

/// A frame that ought to be sent by a link implementation.
/// Potentially holds an interior lock to the underlying link, so this should be released before
/// calling into another link.
#[derive(Debug)]
pub struct SendFrame<'a>(SendFrameInner<'a>);

#[derive(Debug)]
enum SendFrameInner<'a> {
    /// Send the frame that's been queued up in the frame output.
    FromFrameOutput(CutexGuard<'a, OutputQueue>, usize),
    /// Send these exact bytes.
    /// Right now this is only needed for frame labels without payloads (specifically pings).
    /// Consequently we restrict the length to just what is needed for those.
    Raw { bytes: [u8; LINK_FRAME_LABEL_MAX_SIZE], length: usize },
    /// Send these (potentially large) number of bytes
    LargeRaw(Vec<u8>),
}

impl<'a> SendFrame<'a> {
    /// Returns the bytes that should be sent on a link.
    pub fn bytes(&self) -> &[u8] {
        match &self.0 {
            SendFrameInner::FromFrameOutput(g, frame) => {
                let frame = &g.frames[*frame];
                &frame.bytes[..frame.length]
            }
            SendFrameInner::Raw { bytes, length } => &bytes[..*length],
            SendFrameInner::LargeRaw(bytes) => &bytes,
        }
    }

    /// Returns a mutable reference to the bytes that should be sent on a link.
    pub fn bytes_mut(&mut self) -> &mut [u8] {
        match &mut self.0 {
            SendFrameInner::FromFrameOutput(g, frame) => {
                let frame = &mut g.frames[*frame];
                &mut frame.bytes[..frame.length]
            }
            SendFrameInner::Raw { bytes, length } => &mut bytes[..*length],
            SendFrameInner::LargeRaw(ref mut bytes) => bytes,
        }
    }

    /// Relinquishes any internally held locks within this object.
    pub fn drop_inner_locks(&mut self) {
        if let SendFrameInner::FromFrameOutput(g, frame) = &self.0 {
            let frame = &g.frames[*frame];
            let new = SendFrameInner::LargeRaw(frame.bytes[..frame.length].to_vec());
            self.0 = new;
        }
    }
}

/// Creates a link configuration description for diagnostics services.
pub type ConfigProducer =
    Box<dyn Send + Sync + Fn() -> Option<fidl_fuchsia_overnet_protocol::LinkConfig>>;

/// Sender for a link
pub struct LinkSender {
    output: Arc<LinkOutput>,
    router: Arc<Router>,
    force_send_source_node_id: bool,
    _tx_send_closed: oneshot::Sender<()>,
    _task: Arc<Task<()>>,
}

/// Receiver for a link
pub struct LinkReceiver {
    output: Arc<LinkOutput>,
    router: Arc<Router>,
    forwarding_table: Arc<Mutex<ForwardingTable>>,
    received_seq: Option<NonZeroU64>,
    tx_control: mpsc::Sender<LinkControlPayload>,
    peer_node_id: Option<NodeId>,
    tx_recv_closed: Option<oneshot::Sender<()>>,
    _task: Arc<Task<()>>,
}

pub(crate) fn new_link(
    node_link_id: NodeLinkId,
    router: &Arc<Router>,
    config: ConfigProducer,
    introduction_facts: LinkIntroductionFacts,
    connecting_link_token: ConnectingLinkToken,
) -> (LinkSender, LinkReceiver) {
    let forwarding_table = Arc::new(Mutex::new(ForwardingTable::empty()));
    let (tx_control, rx_control) = mpsc::channel(0);
    let first_seq = rand::thread_rng().gen_range(1u64..0xff00_0000_0000_0000);
    let (tx_send_closed, rx_send_closed) = oneshot::channel();
    let (tx_recv_closed, rx_recv_closed) = oneshot::channel();
    let output = Arc::new(LinkOutput {
        queue: Cutex::new(OutputQueue {
            control_acked_seq: first_seq,
            control_sent_seq: first_seq,
            received_any_ack: false,
            frames: Default::default(),
            first_frame: 0,
            num_frames: 0,
            open: true,
            peer_node_id: None,
            ping_tracker: PingTracker::new(),
        }),
        stats: LinkStats {
            packets_forwarded: AtomicU64::new(0),
            pings_sent: AtomicU64::new(0),
            received_bytes: AtomicU64::new(0),
            sent_bytes: AtomicU64::new(0),
            received_packets: AtomicU64::new(0),
            sent_packets: AtomicU64::new(0),
        },
        own_node_id: router.node_id(),
        node_link_id,
        config,
    });
    let run_link = futures::future::try_join(
        run_link(
            Arc::downgrade(&router),
            output.clone(),
            rx_control,
            router.new_forwarding_table_observer(),
            forwarding_table.clone(),
            introduction_facts,
            connecting_link_token,
            rx_send_closed,
            rx_recv_closed,
        ),
        check_ping_timeouts(output.clone()),
    )
    .map_ok(drop);
    let task = Arc::new(Task::spawn(log_errors(
        run_link,
        format!("link {:?} run_loop failed", node_link_id),
    )));
    (
        LinkSender {
            output: output.clone(),
            router: router.clone(),
            force_send_source_node_id: true,
            _tx_send_closed: tx_send_closed,
            _task: task.clone(),
        },
        LinkReceiver {
            output,
            peer_node_id: None,
            router: router.clone(),
            forwarding_table,
            received_seq: None,
            tx_control,
            tx_recv_closed: Some(tx_recv_closed),
            _task: task,
        },
    )
}

async fn run_link(
    router: Weak<Router>,
    output: Arc<LinkOutput>,
    input: mpsc::Receiver<LinkControlPayload>,
    forwarding_table: Observer<ForwardingTable>,
    forwarding_forwarding_table: Arc<Mutex<ForwardingTable>>,
    introduction_facts: LinkIntroductionFacts,
    connecting_link_token: ConnectingLinkToken,
    rx_send_closed: oneshot::Receiver<()>,
    rx_recv_closed: oneshot::Receiver<()>,
) -> Result<(), Error> {
    let inner = run_link_inner(
        router,
        output.clone(),
        input,
        forwarding_table,
        forwarding_forwarding_table,
        introduction_facts,
        connecting_link_token,
    );
    pin_mut!(inner);
    let r = match futures::future::select(
        inner,
        futures::future::select(rx_send_closed, rx_recv_closed),
    )
    .await
    {
        Either::Left((r, _)) => r,
        Either::Right(_) => Err(format_err!("link closed")),
    };

    output.queue.lock().await.open = false;

    r
}

async fn run_link_inner(
    router: Weak<Router>,
    output: Arc<LinkOutput>,
    mut input: mpsc::Receiver<LinkControlPayload>,
    forwarding_table: Observer<ForwardingTable>,
    forwarding_forwarding_table: Arc<Mutex<ForwardingTable>>,
    introduction_facts: LinkIntroductionFacts,
    connecting_link_token: ConnectingLinkToken,
) -> Result<(), Error> {
    let get_router = || -> Result<Arc<Router>, Error> {
        Weak::upgrade(&router).ok_or(format_err!("router gone"))
    };

    tracing::trace!(
        node_id = get_router()?.node_id().0,
        debug_id = ?output.debug_id(),
        "perform link handshake"
    );
    let client_routing = get_router()?.client_routing();
    let (peer_node_id, _peer_introduction_facts) =
        link_handshake(&output, &mut input, introduction_facts).await?;
    tracing::trace!(
        node_id = get_router()?.node_id().0,
        debug_id = ?output.debug_id(),
        ?peer_node_id,
        "link handshake completed",
    );

    let link_routing = Arc::new(LinkRouting { peer_node_id, output: output.clone() });

    let rtt_observable = Observable::new(None);

    futures::future::try_join4(
        get_router()?.publish_link(
            link_routing.clone(),
            rtt_observable.new_observer(),
            connecting_link_token,
        ),
        publish_rtt(output.clone(), rtt_observable),
        send_state(
            output,
            peer_node_id,
            forwarding_table,
            forwarding_forwarding_table,
            client_routing,
        ),
        process_control(input, peer_node_id, router),
    )
    .await
    .map(drop)
}

async fn check_ping_timeouts(output: Arc<LinkOutput>) -> Result<(), Error> {
    loop {
        let mut timeout = output
            .queue
            .lock_when(|output_queue: &OutputQueue| {
                output_queue.ping_tracker.next_timeout().is_some()
            })
            .await
            .ping_tracker
            .next_timeout()
            .unwrap();
        let timeout_expired = loop {
            let timeout_changed = output.queue.lock_when(move |output_queue: &OutputQueue| {
                output_queue.ping_tracker.next_timeout() != Some(timeout)
            });
            pin_mut!(timeout_changed);
            match futures::future::select(&mut timeout_changed, Timer::new(timeout)).await {
                Either::Left((output_queue, _)) => {
                    if let Some(new_timeout) = output_queue.ping_tracker.next_timeout() {
                        timeout = new_timeout;
                    } else {
                        // Wait until there's an actual timeout again.
                        break false;
                    }
                }
                Either::Right(_) => break true,
            }
        };
        if timeout_expired {
            output.queue.lock().await.ping_tracker.on_timeout();
        }
    }
}

async fn publish_rtt(
    output: Arc<LinkOutput>,
    rtt_observable: Observable<Option<Duration>>,
) -> Result<(), Error> {
    let mut last_rtt = None;
    loop {
        let rtt = output
            .queue
            .lock_when(|output_queue: &OutputQueue| {
                output_queue.ping_tracker.round_trip_time() != last_rtt
            })
            .await
            .ping_tracker
            .round_trip_time();
        rtt_observable.push(rtt).await;
        last_rtt = rtt;
    }
}

async fn link_handshake(
    output: &Arc<LinkOutput>,
    input: &mut mpsc::Receiver<LinkControlPayload>,
    introduction_facts: LinkIntroductionFacts,
) -> Result<(NodeId, LinkIntroductionFacts), Error> {
    futures::future::try_join3(
        async move {
            output
                .send_control_message(LinkControlPayload::Introduction(
                    introduction_facts.into_message(),
                ))
                .await?;
            Ok(())
        },
        async move {
            match input.next().await {
                None => bail!("No introduction received"),
                Some(LinkControlPayload::Introduction(introduction)) => {
                    LinkIntroductionFacts::from_message(introduction)
                }
                Some(x) => bail!("Bad initial payload; expected introduction, got {:?}", x),
            }
        },
        async move {
            let peer_node_id = output
                .queue
                .lock_when_pinned(Pin::new(&HAS_PEER_NODE_ID))
                .await
                .peer_node_id
                .unwrap();
            Ok(peer_node_id)
        },
    )
    .await
    .map(|((), intro, id)| (id, intro))
}

/// Process control messages and react to them
async fn process_control(
    mut input: mpsc::Receiver<LinkControlPayload>,
    peer_node_id: NodeId,
    router: Weak<Router>,
) -> Result<(), Error> {
    let get_router = || -> Result<Arc<Router>, Error> {
        Weak::upgrade(&router).ok_or(format_err!("router gone"))
    };

    let mut route_updates = Vec::new();
    loop {
        let payload = input.next().await.ok_or(format_err!("control message channel closed"))?;
        tracing::trace!("got control payload from {:?}: {:?}", peer_node_id, payload);
        match payload {
            LinkControlPayload::Introduction { .. } => bail!("Received second introduction"),
            LinkControlPayload::SetRoute(SetRoute { routes, is_end }) => {
                route_updates.extend_from_slice(&*routes);
                if is_end {
                    get_router()?
                        .update_routes(
                            peer_node_id,
                            std::mem::take(&mut route_updates).into_iter().map(
                                |Route { destination, route_metrics }| {
                                    (destination.into(), route_metrics)
                                },
                            ),
                        )
                        .await
                }
            }
        }
    }
}

/// Background task that watches for forwarding table updates from the router and sends them
/// to this links peer.
async fn send_state(
    output: Arc<LinkOutput>,
    peer_node_id: NodeId,
    mut forwarding_table: Observer<ForwardingTable>,
    forwarding_forwarding_table: Arc<Mutex<ForwardingTable>>,
    client_routing: AscenddClientRouting,
) -> Result<(), Error> {
    let mut last_emitted = ForwardingTable::empty();
    loop {
        tracing::trace!(debug_id = ?output.debug_id(), "await forwarding_table");
        let forwarding_table = forwarding_table
            .next()
            .await
            .ok_or(format_err!("forwarding tables no longer being produced"))?;
        tracing::trace!(
            debug_id = ?output.debug_id(),
            peer_node_id = peer_node_id.0,
            "got forwarding_table: {:?}",
            forwarding_table,
        );
        *forwarding_forwarding_table.lock().await = forwarding_table.clone();
        // Remove any routes that would cause a loop to form.
        let mut forwarding_table = forwarding_table.filter_out_via(peer_node_id);
        if let AscenddClientRouting::Disabled = client_routing {
            // Remove any routes from one ascendd client to another
            if output.is_ascendd_client() {
                forwarding_table = forwarding_table.filter_out_clients();
            }
        }
        // Only send an update if the delta is 'significant' -- either routes have changed,
        // or metrics have changed so significantly that downstream routes are likely to need
        // to be updated (this is a heuristic).
        if forwarding_table.is_significantly_different_to(&last_emitted) {
            tracing::trace!(
                debug_id = ?output.debug_id(),
                "Send new forwarding table: {:?}",
                forwarding_table
            );
            let empty_output = || SetRoute { is_end: false, routes: Vec::new() };
            let mut set_route = empty_output();
            for (destination, metrics) in forwarding_table.iter() {
                set_route
                    .routes
                    .push(Route { destination: destination.into(), route_metrics: metrics.into() });
                if encode_fidl_with_context(coding::DEFAULT_CONTEXT, &mut set_route)?.len()
                    > MAX_SET_ROUTE_LENGTH
                {
                    let route = set_route.routes.pop().unwrap();
                    output
                        .send_control_message(LinkControlPayload::SetRoute(std::mem::replace(
                            &mut set_route,
                            empty_output(),
                        )))
                        .await?;
                    set_route.routes.push(route);
                    assert!(
                        encode_fidl_with_context(coding::DEFAULT_CONTEXT, &mut set_route)?.len()
                            <= MAX_SET_ROUTE_LENGTH
                    );
                }
            }
            set_route.is_end = true;
            output.send_control_message(LinkControlPayload::SetRoute(set_route)).await?;
            last_emitted = forwarding_table;
        }
    }
}

/// IO for a link
struct LinkOutput {
    queue: Cutex<OutputQueue>,
    stats: LinkStats,
    own_node_id: NodeId,
    node_link_id: NodeLinkId,
    config: ConfigProducer,
}

impl LinkOutput {
    fn debug_id(&self) -> impl std::fmt::Debug {
        self.node_link_id
    }

    pub(crate) async fn is_closed(&self) -> bool {
        !self.queue.lock().await.open
    }

    pub(crate) fn is_ascendd_client(&self) -> bool {
        match (self.config)() {
            Some(LinkConfig::AscenddServer(_)) => true,
            _ => false,
        }
    }

    /// Send a control message to our peer with some payload.
    /// Implements periodic resends until an ack is received.
    async fn send_control_message(&self, payload: LinkControlPayload) -> Result<(), Error> {
        const DEFAULT_RTT: Duration = Duration::from_millis(50);

        let new_resend_delay = |current_resend_delay: Duration, ping_tracker: &PingTracker| {
            let new = std::cmp::max(
                3 * current_resend_delay / 2,
                2 * ping_tracker.round_trip_time().unwrap_or(DEFAULT_RTT),
            );
            if new < MIN_RESEND_DELAY {
                MIN_RESEND_DELAY
            } else if new > MAX_RESEND_DELAY {
                MAX_RESEND_DELAY
            } else {
                new
            }
        };

        let mut output = self.queue.lock_when_pinned(Pin::new(&READY_TO_SEND_NEW_CONTROL)).await;
        let seq = output.control_sent_seq + 1;
        let mut frame = LinkControlFrame::Message(LinkControlMessage { seq, payload });
        let coding_context = coding::DEFAULT_CONTEXT;
        let message = encode_fidl_with_context(coding_context, &mut frame)?;
        output
            .send(RoutingTarget {
                src: self.own_node_id,
                dst: RoutingDestination::Control(coding_context),
            })?
            .commit_copy(&message)?;
        output.control_sent_seq = seq;
        let mut resend_delay = new_resend_delay(Duration::from_millis(0), &output.ping_tracker);
        drop(output);

        // Resend periodically until acked or we convince ourselves it's never going to happen.
        async move {
            let done_predicate = AckedControlSeq(seq);
            pin_mut!(done_predicate);
            loop {
                match futures::future::select(
                    self.queue.lock_when_pinned(done_predicate.as_ref()),
                    Timer::new(resend_delay),
                )
                .await
                {
                    Either::Left(_) => {
                        return Ok(());
                    }
                    Either::Right((_, lock)) => {
                        drop(lock);
                        let mut output =
                            self.queue.lock_when_pinned(Pin::new(&READY_TO_RESEND_CONTROL)).await;
                        if output.control_acked_seq >= seq {
                            return Ok(());
                        }
                        output
                            .send(RoutingTarget {
                                src: self.own_node_id,
                                dst: RoutingDestination::Control(coding_context),
                            })?
                            .commit_copy(&message)?;
                        resend_delay = new_resend_delay(resend_delay, &output.ping_tracker);
                    }
                }
            }
        }
        .on_timeout(MAX_CONTROL_MESSAGE_RETRY_TIME, || {
            Err(format_err!("Timeout sending control message"))
        })
        .await
    }
}

/// Routing data for a link
pub(crate) struct LinkRouting {
    peer_node_id: NodeId,
    output: Arc<LinkOutput>,
}

impl std::fmt::Debug for LinkRouting {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "LINK({:?}:{:?}->{:?})", self.id(), self.own_node_id(), self.peer_node_id())
    }
}

impl LinkRouting {
    pub(crate) fn id(&self) -> NodeLinkId {
        self.output.node_link_id
    }

    pub(crate) fn own_node_id(&self) -> NodeId {
        self.output.own_node_id
    }

    pub(crate) fn peer_node_id(&self) -> NodeId {
        self.peer_node_id
    }

    pub(crate) fn debug_id(&self) -> impl std::fmt::Debug {
        (self.id(), self.own_node_id(), self.peer_node_id())
    }

    pub(crate) async fn is_closed(&self) -> bool {
        self.output.is_closed().await
    }

    pub(crate) fn is_ascendd_client(&self) -> bool {
        self.output.is_ascendd_client()
    }

    pub(crate) async fn diagnostic_info(&self) -> LinkDiagnosticInfo {
        let stats = &self.output.stats;
        LinkDiagnosticInfo {
            source: Some(self.own_node_id().into()),
            destination: Some(self.peer_node_id().into()),
            source_local_id: Some(self.id().0),
            sent_packets: Some(stats.sent_packets.load(Ordering::Relaxed)),
            received_packets: Some(stats.received_packets.load(Ordering::Relaxed)),
            sent_bytes: Some(stats.sent_bytes.load(Ordering::Relaxed)),
            received_bytes: Some(stats.received_bytes.load(Ordering::Relaxed)),
            pings_sent: Some(stats.pings_sent.load(Ordering::Relaxed)),
            packets_forwarded: Some(stats.packets_forwarded.load(Ordering::Relaxed)),
            round_trip_time_microseconds: self
                .output
                .queue
                .lock()
                .await
                .ping_tracker
                .round_trip_time()
                .map(|d| d.as_micros().try_into().unwrap_or(std::u64::MAX)),
            config: (self.output.config)(),
            ..LinkDiagnosticInfo::EMPTY
        }
    }

    /// Produce a ticket to acquire a cutex once it becomes sendable for a message.
    pub(crate) fn new_message_send_ticket<'a>(&'a self) -> CutexTicket<'a, 'static, OutputQueue> {
        CutexTicket::new_when_pinned(&self.output.queue, Pin::new(&READY_TO_SEND_MESSAGE))
    }
}

impl LinkReceiver {
    /// Returns some moniker identifying the underlying link, for use in debugging.
    pub fn debug_id(&self) -> impl std::fmt::Debug {
        self.output.node_link_id
    }

    /// Remove the label from a frame.
    async fn remove_label<'a>(
        &mut self,
        frame: &'a mut [u8],
    ) -> Result<Option<(LinkFrameLabel, &'a mut [u8])>, RecvError> {
        let output = &*self.output;
        let stats = &output.stats;

        stats.received_packets.fetch_add(1, Ordering::Relaxed);
        stats.received_bytes.fetch_add(frame.len() as u64, Ordering::Relaxed);
        if frame.len() < 1 {
            return Err(RecvError::Warning(format_err!("Received empty frame")));
        }
        let (routing_label, frame_length) =
            LinkFrameLabel::decode(self.peer_node_id, output.own_node_id, frame)
                .with_context(|| format_err!("Decoding routing label"))
                .map_err(RecvError::Fatal)?;
        let frame = &mut frame[..frame_length];
        if routing_label.ping.is_some() || routing_label.pong.is_some() {
            output
                .queue
                .lock()
                .await
                .ping_tracker
                .on_received_frame(routing_label.ping, routing_label.pong);
        }
        if frame.len() == 0 {
            // Packet was just control bits
            return Ok(None);
        }

        Ok(Some((routing_label, frame)))
    }

    async fn handle_control(
        &mut self,
        src: NodeId,
        frame: &mut [u8],
        coding_context: coding::Context,
    ) -> Result<(), RecvError> {
        let output = &self.output;

        if let Some(last_seen_src) = self.peer_node_id {
            if last_seen_src != src {
                return Err(RecvError::Fatal(format_err!(
                    "[{:?}] link source address changed from {:?} to {:?}",
                    self.debug_id(),
                    last_seen_src,
                    src
                )));
            }
        } else {
            self.peer_node_id = Some(src);
            output.queue.lock().await.peer_node_id = Some(src);
        }

        let frame = decode_fidl_with_context(coding_context, frame)?;
        match frame {
            LinkControlFrame::Ack(seq) => {
                let mut frame_output = output.queue.lock().await;
                if seq == frame_output.control_sent_seq {
                    frame_output.received_any_ack = true;
                    frame_output.control_acked_seq = seq;
                }
            }
            LinkControlFrame::Message(LinkControlMessage { seq: 0, .. }) => {
                return Err(RecvError::Fatal(format_err!(
                    "[{:?}] Saw a control message with seq 0",
                    self.debug_id()
                )));
            }
            LinkControlFrame::Message(LinkControlMessage { seq, payload }) => {
                if let Some(received_seq) = self.received_seq {
                    let received_seq = received_seq.into();
                    if seq == received_seq {
                        // Ignore but fall through to ack code below.
                    } else if seq == received_seq + 1 {
                        self.tx_control.send(payload).await.map_err(|_| {
                            format_err!("failed queueing control message for processing")
                        })?;
                    } else if seq > received_seq {
                        return Err(RecvError::Fatal(format_err!(
                            "[{:?}] saw future message seq={} but we are at {}",
                            self.debug_id(),
                            seq,
                            received_seq
                        )));
                    } else if seq >= received_seq - 2 {
                        return Err(RecvError::Fatal(format_err!(
                            "[{:?}] saw ancient message seq={} but we are at {}",
                            self.debug_id(),
                            seq,
                            received_seq
                        )));
                    } else {
                        return Err(RecvError::Warning(format_err!(
                            "[{:?}] saw old message seq={} but we are at {}",
                            self.debug_id(),
                            seq,
                            received_seq
                        )));
                    }
                } else {
                    self.tx_control.send(payload).await.map_err(|_| {
                        format_err!("failed queueing control message for processing")
                    })?;
                }
                self.received_seq = Some(seq.try_into().unwrap());
                let ack = encode_fidl_with_context(
                    coding::DEFAULT_CONTEXT,
                    &mut LinkControlFrame::Ack(seq),
                )?;
                output
                    .queue
                    .lock_when_pinned(Pin::new(&READY_TO_RESEND_CONTROL))
                    .await
                    .send(RoutingTarget {
                        src: output.own_node_id,
                        dst: RoutingDestination::Control(coding_context),
                    })?
                    .commit_copy(&ack)?;
            }
        }
        Ok(())
    }

    async fn handle_message(&mut self, src: NodeId, frame: &mut [u8]) -> Result<(), Error> {
        let hdr =
            quiche::Header::from_slice(frame, quiche::MAX_CONN_ID_LEN).with_context(|| {
                format!("Decoding quic header; link={:?}; src={:?}", self.debug_id(), src)
            })?;
        let peer =
            self.router.lookup_peer_for_link(&hdr.dcid, hdr.ty, src).await.with_context(|| {
                format!("link={:?}; src={:?}; hdr={:?}", self.debug_id(), src, hdr)
            })?;
        peer.receive_frame(frame).await.with_context(|| {
            format!(
                concat!(
                    "Receiving frame on quic connection;",
                    " peer node={:?} endpoint={:?};",
                    " hdr={:?}"
                ),
                peer.node_id(),
                peer.endpoint(),
                hdr,
            )
        })?;
        Ok(())
    }

    async fn forward_message(
        &mut self,
        src: NodeId,
        dst: NodeId,
        frame: &mut [u8],
    ) -> Result<(), Error> {
        if let Some(via) = self.forwarding_table.lock().await.route_for(dst) {
            tracing::trace!(src = src.0, dst = dst.0, ?via, "fwd");
            if let Some(via) = self.router.get_link(via).await {
                if via.output.node_link_id == self.output.node_link_id || via.peer_node_id == src {
                    // This is a looped frame - signal to the sender to avoid this and drop it
                    tracing::trace!(debug_id = ?self.debug_id(), "Dropping frame due to routing loop");
                    return Ok(());
                }
                via.output
                    .queue
                    .lock_when_pinned(Pin::new(&READY_TO_SEND_MESSAGE))
                    .await
                    .send(RoutingTarget { src, dst: RoutingDestination::Message(dst) })?
                    .commit_copy(frame)?;
            } else {
                tracing::trace!(debug_id = ?self.debug_id(), "Dropping frame because no via");
            }
        } else {
            tracing::trace!(src = src.0, dst = dst.0, "Drop forwarded packet - no route to dest");
        }
        Ok(())
    }

    async fn received_frame_inner(&mut self, frame: &mut [u8]) -> Result<(), RecvError> {
        if let Some((routing_label, frame)) = self.remove_label(frame).await? {
            let src = routing_label.target.src;
            match routing_label.target.dst {
                RoutingDestination::Control(coding_context) => {
                    self.handle_control(src, frame, coding_context).await?
                }
                RoutingDestination::Message(dst) => {
                    let own_node_id = self.output.own_node_id;
                    if src == own_node_id {
                        // Got a frame that was sourced here: break the infinite loop by definitely not
                        // processing it.
                        return Err(RecvError::Warning(format_err!(
                            "[{:?}] Received looped frame; routing_label={:?}",
                            self.debug_id(),
                            routing_label
                        )));
                    }
                    if dst == own_node_id {
                        self.handle_message(src, frame).await?;
                    } else {
                        self.forward_message(src, dst, frame).await?;
                    }
                }
            };
        }
        Ok(())
    }

    /// Report a frame was received.
    pub async fn received_frame(&mut self, frame: &mut [u8]) {
        match self.received_frame_inner(frame).await {
            Ok(()) => (),
            Err(RecvError::Warning(err)) => {
                tracing::info!(
                    debug_id = ?self.debug_id(),
                    "Recoverable error receiving frame: {:?}",
                    err
                )
            }
            Err(RecvError::Fatal(err)) => {
                tracing::warn!(
                    debug_id = ?self.debug_id(),
                    "Link-fatal error receiving frame: {:?}",
                    err
                );
                self.tx_recv_closed.take();
            }
        }
    }

    /// Report the peer's node id if it is known, or None if it is not yet.
    pub fn peer_node_id(&self) -> Option<NodeId> {
        self.peer_node_id
    }
}

enum RecvError {
    Warning(Error),
    Fatal(Error),
}

impl From<Error> for RecvError {
    fn from(err: Error) -> Self {
        Self::Warning(err)
    }
}

impl LinkSender {
    /// Returns a reference to the router
    pub fn router(&self) -> &Router {
        &*self.router
    }

    /// Returns some moniker identifying the underlying link, for use in debugging.
    pub fn debug_id(&self) -> impl std::fmt::Debug {
        self.output.node_link_id
    }

    /// Retrieve the next frame that should be sent via this link.
    /// Returns: Some(p) to send a packet `p`
    ///          None to indicate link closure
    pub async fn next_send(&mut self) -> Option<SendFrame<'_>> {
        let output = &self.output;
        let stats = &output.stats;
        let mut output_queue = output
            .queue
            .lock_when(|output_queue: &OutputQueue| {
                !output_queue.open
                    || output_queue.num_frames > 0
                    || output_queue.ping_tracker.needs_send()
            })
            .await;
        let (ping, pong) = output_queue.ping_tracker.pull_send();
        if !output_queue.open {
            None
        } else if output_queue.num_frames > 0 {
            if ping.is_some() {
                stats.pings_sent.fetch_add(1, Ordering::Relaxed);
            }
            let peer_node_id = output_queue.peer_node_id;
            self.force_send_source_node_id = !output_queue.received_any_ack;
            let frame_index = output_queue.first_frame;
            let frame = &mut output_queue.frames[frame_index];
            let target = frame.target;
            let label = LinkFrameLabel {
                target,
                ping,
                pong,
                debug_token: LinkFrameLabel::new_debug_token(),
            };
            let original_length = frame.length;
            let n_tail = label
                .encode_for_link(
                    output.own_node_id,
                    self.force_send_source_node_id,
                    peer_node_id,
                    &mut frame.bytes[original_length..],
                )
                .expect("encode_for_link should always succeed");
            frame.length += n_tail;
            stats.sent_packets.fetch_add(1, Ordering::Relaxed);
            stats.sent_bytes.fetch_add(frame.length as u64, Ordering::Relaxed);
            output_queue.first_frame = (output_queue.first_frame + 1) % MAX_QUEUED_FRAMES;
            output_queue.num_frames -= 1;
            // SendFrame continues to hold the OutputQueue cutex.
            Some(SendFrame(SendFrameInner::FromFrameOutput(output_queue, frame_index)))
        } else {
            assert!(ping.is_some() || pong.is_some());
            if ping.is_some() {
                stats.pings_sent.fetch_add(1, Ordering::Relaxed);
            }
            let label = LinkFrameLabel {
                target: RoutingTarget {
                    src: output.own_node_id,
                    dst: RoutingDestination::Control(coding::DEFAULT_CONTEXT),
                },
                ping,
                pong,
                debug_token: LinkFrameLabel::new_debug_token(),
            };
            tracing::trace!(link = ?self.debug_id(), deliver = ?label);
            let mut bytes = [0u8; LINK_FRAME_LABEL_MAX_SIZE];
            let length = label
                .encode_for_link(
                    output.own_node_id,
                    self.force_send_source_node_id,
                    None,
                    &mut bytes[..],
                )
                .expect("encode_for_link should always succeed");
            stats.sent_packets.fetch_add(1, Ordering::Relaxed);
            stats.sent_bytes.fetch_add(length as u64, Ordering::Relaxed);
            Some(SendFrame(SendFrameInner::Raw { bytes, length }))
        }
    }
}
