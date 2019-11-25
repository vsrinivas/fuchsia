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
    coding::decode_fidl,
    labels::{NodeId, NodeLinkId, RoutingLabel, MAX_ROUTING_LABEL_LENGTH},
    node_table::{LinkDescription, NodeDescription},
    ping_tracker::{PingTracker, PingTrackerResult, PongTracker},
};
use failure::{Error, ResultExt};
use fidl_fuchsia_overnet_protocol::{
    ChannelHandle, ConnectToService, ConnectToServiceOptions, LinkDiagnosticInfo, LinkMetrics,
    LinkStatus, PeerConnectionDiagnosticInfo, PeerDescription, PeerMessage, PeerReply,
    UpdateLinkStatus, ZirconChannelMessage, ZirconHandle,
};
use rand::Rng;
use salt_slab::{SaltSlab, SaltedID, ShadowSlab};
use std::{
    collections::{btree_map, BTreeMap, BTreeSet, BinaryHeap},
    convert::TryInto,
    fmt::Debug,
    path::Path,
    time::Instant,
};

/// Designates one peer in the router
pub type PeerId<LinkData> = SaltedID<Peer<LinkData>>;
/// Designates one link in the router
pub type LinkId<LinkData> = SaltedID<Link<LinkData>>;
/// Designates one stream in the router
pub type StreamId<LinkData> = SaltedID<Stream<LinkData>>;

/// Describes the current time for a router.
pub trait RouterTime: PartialEq + PartialOrd + Ord + Clone + Copy {
    /// Representation of the distance between two times.
    type Duration: From<std::time::Duration>;

    /// Return the current time.
    fn now() -> Self;
    /// Return the time after some duration.
    fn after(time: Self, duration: Self::Duration) -> Self;
}

impl RouterTime for Instant {
    type Duration = std::time::Duration;

    fn now() -> Self {
        Instant::now()
    }

    fn after(time: Self, duration: Self::Duration) -> Self {
        time + duration
    }
}

#[derive(Debug)]
enum PeerLinkStatusUpdateState {
    Unscheduled,
    Sent,
    SentOutdated,
}

struct PeerConn(Box<quiche::Connection>);

#[derive(Debug)]
struct PeerConnDebug<'a> {
    trace_id: &'a str,
    application_proto: &'a [u8],
    is_established: bool,
    is_resumed: bool,
    is_closed: bool,
    stats: quiche::Stats,
}

impl std::fmt::Debug for PeerConn {
    fn fmt(&self, fmt: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        PeerConnDebug {
            trace_id: self.0.trace_id(),
            application_proto: self.0.application_proto(),
            is_established: self.0.is_established(),
            is_resumed: self.0.is_resumed(),
            is_closed: self.0.is_closed(),
            stats: self.0.stats(),
        }
        .fmt(fmt)
    }
}

/// Describes a peer to this router.
/// For each peer we manage a quic connection and a set of streams over that quic connection.
/// For each node in the mesh we keep two peers: one client oriented, one server oriented (using
/// quic's notion of client/server).
/// ConnectToService requests travel from the client -> server, and all subsequent streams are
/// created on that connection.
/// See Router for a description of LinkData.
#[derive(Debug)]
pub struct Peer<LinkData: Copy + Debug> {
    /// The quic connection between ourselves and this peer.
    conn: PeerConn,
    /// The stream id of the connection stream for this peer.
    connection_stream_id: StreamId<LinkData>,
    /// The next stream index to create for an outgoing stream.
    next_stream: u64,
    /// The node associated with this peer.
    node_id: NodeId,
    /// Is this client or server oriented?
    is_client: bool,
    /// Are there writes pending on this peer (for the next flush_writes).
    pending_writes: bool,
    /// Are there reads pending on this peer (for the next flush_reads).
    pending_reads: bool,
    /// Should we check for an updated timeout for this peer?
    check_timeout: bool,
    /// Is this peer still establishing a link?
    establishing: bool,
    /// We keep a timeout generation so we can avoid calling conn.on_timeout for irrelevant
    /// timeout requests, without having to do a deletion in the timer data structure.
    timeout_generation: u64,
    /// A list of packets to forward on next writing flush.
    forward: Vec<(RoutingLabel, Vec<u8>)>,
    /// A map of quic stream index to overnet stream id for this peer.
    streams: BTreeMap<u64, StreamId<LinkData>>,
    /// If known, a link that will likely be able to forward packets to this peers node_id.
    current_link: Option<LinkId<LinkData>>,
    /// State of link status updates for this peer (only for client)
    link_status_update_state: Option<PeerLinkStatusUpdateState>,
    // Stats, per PeerConnectionDiagnosticsInfo
    messages_sent: u64,
    bytes_sent: u64,
    connect_to_service_sends: u64,
    connect_to_service_send_bytes: u64,
    update_node_description_sends: u64,
    update_node_description_send_bytes: u64,
    update_link_status_sends: u64,
    update_link_status_send_bytes: u64,
    update_link_status_ack_sends: u64,
    update_link_status_ack_send_bytes: u64,
}

/// Was a given stream index initiated by this end of a quic connection?
fn is_local_quic_stream_index(stream_index: u64, is_client: bool) -> bool {
    (stream_index & 1) == (!is_client as u64)
}

/// Describes a stream from this node to some peer.
#[derive(Debug)]
pub struct Stream<LinkData: Copy + Debug> {
    /// The peer that terminates this stream.
    peer_id: PeerId<LinkData>,
    /// The quic stream id associated with this stream (The reverse map of Peer.streams)
    id: u64,
    /// The current parse state for this stream
    read_state: ReadState,
}

/// Helper to swizzle a u64 into the wire format for zircon handles
fn zircon_stream_id(id: u64) -> fidl_fuchsia_overnet_protocol::StreamId {
    fidl_fuchsia_overnet_protocol::StreamId { id }
}

/// Designates the kind of stream for a read state (what kind of callback should be made
/// when a datagram completes!)
#[derive(Clone, Copy, Debug)]
enum StreamType {
    PeerClientEnd,
    PeerServerEnd,
    Channel,
    // TODO(ctiller): Socket,
}

/// Tracks incoming bytes as they're coalesced back into datagrams
#[derive(Debug)]
enum ReadState {
    /// Stream is not bound to any object: we don't know what parsers to apply to its
    /// bytes yet, so we just buffer them up.
    Unbound { incoming: Vec<u8> },
    /// Stream type is known, but we don't yet know the length of the next datagram.
    Initial { incoming: Vec<u8>, stream_type: StreamType },
    /// Stream type is known, and the length of the next datagram has been parsed.
    /// We do not yet know all the bytes for said datagram however.
    Datagram { len: usize, incoming: Vec<u8>, stream_type: StreamType },
    /// Stream has finished successfully.
    FinishedOk,
    /// Stream completed with an error.
    /// TODO(ctiller): record the error?
    FinishedWithError,
    /// Only used during updating - to mark that the state update is in progress.
    Invalid,
}

impl ReadState {
    /// Create a new read state for a stream of a known type.
    fn new_bound(stream_type: StreamType) -> ReadState {
        ReadState::Initial { incoming: Vec::new(), stream_type }
    }

    /// Create a new read state for a stream of an unknown type.
    fn new_unbound() -> ReadState {
        ReadState::Unbound { incoming: Vec::new() }
    }

    /// Begin updating a read state.
    fn take(&mut self) -> ReadState {
        std::mem::replace(self, ReadState::Invalid)
    }

    /// Upon receiving some bytes, update state and return the new one.
    fn recv<Got>(self, bytes: &mut [u8], mut got: Got) -> ReadState
    where
        Got: FnMut(StreamType, Option<&mut [u8]>) -> Result<(), Error>,
    {
        if bytes.len() == 0 {
            return self;
        }

        let rdlen = |buf: &[u8]| {
            (buf[0] as u32)
                | ((buf[1] as u32) << 8)
                | ((buf[2] as u32) << 16)
                | ((buf[3] as u32) << 24)
        };

        log::trace!("recv bytes: state={:?} bytes={:?}", self, bytes);

        match self {
            ReadState::Unbound { mut incoming } => {
                incoming.extend_from_slice(bytes);
                ReadState::Unbound { incoming }
            }
            ReadState::Initial { mut incoming, stream_type } => {
                // TODO(ctiller): consider specializing the fast path
                // if incoming.len() == 0 && bytes.len() > 4 {}
                incoming.extend_from_slice(bytes);
                if incoming.len() > 4 {
                    let len = rdlen(&mut incoming) as usize;
                    ReadState::Datagram { len, incoming: vec![], stream_type }
                        .recv(&mut incoming[4..], got)
                } else {
                    ReadState::Initial { incoming, stream_type }
                }
            }
            ReadState::Datagram { mut incoming, len, stream_type } => {
                incoming.extend_from_slice(bytes);
                if incoming.len() >= len {
                    match got(stream_type, Some(&mut incoming[..len])) {
                        Ok(_) => ReadState::Initial { incoming: vec![], stream_type }
                            .recv(&mut incoming[len..], got),
                        Err(e) => {
                            log::trace!("Error reading stream: {:?}", e);
                            got(stream_type, None).unwrap();
                            ReadState::FinishedWithError
                        }
                    }
                } else {
                    ReadState::Datagram { incoming, len, stream_type }
                }
            }
            ReadState::FinishedOk => ReadState::FinishedWithError,
            ReadState::FinishedWithError => ReadState::FinishedWithError,
            ReadState::Invalid => unreachable!(),
        }
    }

    /// Go from unbound -> bound when the stream type is known.
    fn bind<Got>(self, stream_type: StreamType, got: Got) -> ReadState
    where
        Got: FnMut(StreamType, Option<&mut [u8]>) -> Result<(), Error>,
    {
        match self {
            ReadState::Unbound { mut incoming } => {
                ReadState::Initial { incoming: Vec::new(), stream_type }.recv(&mut incoming, got)
            }
            ReadState::Invalid => unreachable!(),
            _ => ReadState::FinishedWithError,
        }
    }

    /// Mark the stream as finished.
    fn finished<Got>(self, mut got: Got) -> Self
    where
        Got: FnMut(),
    {
        match self {
            ReadState::Initial { incoming, stream_type: _ } => {
                got();
                // If we didn't receive all bytes, this is an error.
                if incoming.is_empty() {
                    ReadState::FinishedOk
                } else {
                    ReadState::FinishedWithError
                }
            }
            ReadState::FinishedOk => ReadState::FinishedOk,
            ReadState::FinishedWithError => ReadState::FinishedWithError,
            ReadState::Invalid => unreachable!(),
            _ => {
                got();
                ReadState::FinishedWithError
            }
        }
    }
}

/// Configuration object for creating a router.
pub struct RouterOptions {
    node_id: Option<NodeId>,
    quic_server_key_file: Option<Box<dyn AsRef<Path>>>,
    quic_server_cert_file: Option<Box<dyn AsRef<Path>>>,
}

impl RouterOptions {
    /// Create with defaults.
    pub fn new() -> Self {
        RouterOptions { node_id: None, quic_server_key_file: None, quic_server_cert_file: None }
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
}

/// During flush_recvs, defines the set of callbacks that may be made to notify the containing
/// application of updates received.
pub trait MessageReceiver<LinkData: Copy + Debug> {
    /// The type of handle this receiver deals with. Usually this is fidl::Handle, but for
    /// some tests it's convenient to vary this.
    type Handle;

    /// A connection request for some channel has been made.
    /// The new stream is `stream_id`, and the service requested is `service_name`.
    fn connect_channel<'a>(
        &mut self,
        stream_id: StreamId<LinkData>,
        service_name: &'a str,
        connection_info: fidl_fuchsia_overnet::ConnectionInfo,
    ) -> Result<(), Error>;
    /// A new channel stream is created at `stream_id`.
    fn bind_channel(&mut self, stream_id: StreamId<LinkData>) -> Result<Self::Handle, Error>;
    /// A channel stream `stream_id` received a datagram with `bytes` and `handles`.
    fn channel_recv(
        &mut self,
        stream_id: StreamId<LinkData>,
        bytes: &mut Vec<u8>,
        handles: &mut Vec<Self::Handle>,
    ) -> Result<(), Error>;
    /// A node `node_id` has updated its description to `desc`.
    fn update_node(&mut self, node_id: NodeId, desc: NodeDescription);
    /// A control channel has been established to node `node_id`
    fn established_connection(&mut self, node_id: NodeId);
    /// Gossip about a link from `from` to `to` with id `link_id` has been received.
    /// This gossip is about version `version`, and updates the links description to
    /// `desc`.
    fn update_link(&mut self, from: NodeId, to: NodeId, link_id: NodeLinkId, desc: LinkDescription);
    /// A stream `stream_id` has been closed.
    fn close(&mut self, stream_id: StreamId<LinkData>);
}

/// When sending a datagram on a channel, contains information needed to establish streams
/// for any handles being sent.
pub enum SendHandle {
    /// A handle of type channel is being sent.
    Channel,
}

/// Router-relevant state of a link.
/// This type is parameterized by `LinkData` which is an application defined type that
/// lets it look up the link implementation quickly.
#[derive(Debug)]
pub struct Link<LinkData: Copy + Debug> {
    /// The node on the other end of this link.
    peer: NodeId,
    /// The externally visible id for this link
    id: NodeLinkId,
    /// Track pings for this link
    ping_tracker: PingTracker,
    /// Track pongs for this link
    pong_tracker: PongTracker,
    /// The application data required to lookup this link.
    link_data: LinkData,
    /// Timeout generation for ping_tracker
    timeout_generation: u64,
    sent_packets: u64,
    sent_bytes: u64,
    received_packets: u64,
    received_bytes: u64,
    pings_sent: u64,
    packets_forwarded: u64,
}

impl<LinkData: Copy + Debug> Link<LinkData> {
    fn make_desc(&self) -> Option<LinkDescription> {
        self.ping_tracker
            .round_trip_time()
            .map(|round_trip_time| LinkDescription { round_trip_time })
    }

    fn make_status(&self) -> Option<LinkStatus> {
        self.make_desc().map(|desc| {
            let round_trip_time = desc.round_trip_time.as_micros();
            LinkStatus {
                local_id: self.id.0,
                to: self.peer.into(),
                metrics: LinkMetrics {
                    round_trip_time: Some(if round_trip_time > std::u64::MAX as u128 {
                        std::u64::MAX
                    } else {
                        round_trip_time as u64
                    }),
                },
            }
        })
    }
}

/// Records a timeout request at some time for some object.
/// The last tuple element is a generation counter so we can detect expired timeouts.
#[derive(Clone, Copy, PartialEq, Eq, Ord)]
struct Timeout<Time: RouterTime, T>(Time, T, u64);

/// Sort time ascending.
impl<Time: RouterTime, T: Ord> PartialOrd for Timeout<Time, T> {
    fn partial_cmp(&self, rhs: &Self) -> Option<std::cmp::Ordering> {
        Some(self.0.cmp(&rhs.0).reverse().then(self.1.cmp(&rhs.1)))
    }
}

/// The part of Router that deals only with streams and peers (but not links).
/// Separated to make satisfying the borrow checker a little more tractable without having to
/// write monster functions.
struct Endpoints<LinkData: Copy + Debug, Time: RouterTime> {
    /// Node id of this router.
    node_id: NodeId,
    /// All peers.
    peers: SaltSlab<Peer<LinkData>>,
    /// All streams.
    streams: SaltSlab<Stream<LinkData>>,
    /// Mapping of node id -> client oriented peer id.
    node_to_client_peer: BTreeMap<NodeId, PeerId<LinkData>>,
    /// Mapping of node id -> server oriented peer id.
    node_to_server_peer: BTreeMap<NodeId, PeerId<LinkData>>,
    /// Peers waiting to be written (has pending_writes==true).
    write_peers: BinaryHeap<PeerId<LinkData>>,
    /// Peers waiting to be read (has pending_reads==true).
    read_peers: BinaryHeap<PeerId<LinkData>>,
    /// Peers that need their timeouts checked (having check_timeout==true).
    check_timeouts: BinaryHeap<PeerId<LinkData>>,
    /// Client oriented peer connections that have become established.
    newly_established_clients: BinaryHeap<NodeId>,
    /// Client oriented peer connections that need to send initial node descriptions to.
    need_to_send_node_description_clients: BinaryHeap<StreamId<LinkData>>,
    /// Peers that we've heard about recently.
    recently_mentioned_peers: Vec<(NodeId, Option<LinkId<LinkData>>)>,
    /// Timeouts to get to at some point.
    queued_timeouts: BinaryHeap<Timeout<Time, PeerId<LinkData>>>,
    /// Link status updates waiting to be sent
    link_status_updates: BinaryHeap<StreamId<LinkData>>,
    /// Link status updates waiting to be acked
    ack_link_status_updates: BinaryHeap<StreamId<LinkData>>,
    /// The last timestamp we saw.
    now: Time,
    /// The current description of our node id, formatted into a serialized description packet
    /// so we can quickly send it at need.
    node_desc_packet: Vec<u8>,
    /// The servers private key file for this node.
    server_key_file: Option<Box<dyn AsRef<Path>>>,
    /// The servers private cert file for this node.
    server_cert_file: Option<Box<dyn AsRef<Path>>>,
}

impl<LinkData: Copy + Debug, Time: RouterTime> Endpoints<LinkData, Time> {
    /// Update the routing table for `dest` to use `link_id`.
    fn adjust_route(&mut self, dest: NodeId, link_id: LinkId<LinkData>) -> Result<(), Error> {
        log::trace!("ADJUST_ROUTE: {:?} via {:?}", dest, link_id);

        let server_peer_id = self.server_peer_id(dest);
        log::trace!("  server_peer_id = {:?}", server_peer_id);
        if let Some(peer_id) = server_peer_id {
            let peer = self.peers.get_mut(peer_id).unwrap();
            if peer.current_link.is_none() && !peer.pending_writes {
                log::trace!("Mark writable: {:?}", peer_id);
                peer.pending_writes = true;
                self.write_peers.push(peer_id);
            }
            peer.current_link = Some(link_id);
        }
        let peer_id = self.client_peer_id(dest, Some(link_id))?;
        let peer = self.peers.get_mut(peer_id).unwrap();
        log::trace!("  client_peer_id = {:?}", peer_id);
        peer.current_link = Some(link_id);
        Ok(())
    }

    /// Return true if there's a client oriented established connection to `dest`.
    fn connected_to(&self, dest: NodeId) -> bool {
        if let Some(peer_id) = self.node_to_client_peer.get(&dest) {
            let peer = self.peers.get(*peer_id).unwrap();
            peer.conn.0.is_established()
        } else {
            false
        }
    }

    /// Create a new stream to advertised service `service` on remote node id `node`.
    fn new_stream(&mut self, node: NodeId, service: &str) -> Result<StreamId<LinkData>, Error> {
        assert_ne!(node, self.node_id);
        let peer_id = self.client_peer_id(node, None)?;
        let peer = self.peers.get_mut(peer_id).unwrap();
        let id = peer
            .next_stream
            .checked_shl(2)
            .ok_or_else(|| failure::format_err!("Too many streams"))?;
        peer.next_stream += 1;
        let connection_stream_id = peer.connection_stream_id;
        let stream_id = self.streams.insert(Stream {
            peer_id,
            id,
            read_state: ReadState::new_bound(StreamType::Channel),
        });
        peer.streams.insert(id, stream_id);
        let err = fidl::encoding::with_tls_encoded(
            &mut PeerMessage::ConnectToService(ConnectToService {
                service_name: service.to_string(),
                stream_id: id,
                options: ConnectToServiceOptions {},
            }),
            |mut bytes: &mut Vec<u8>, handles: &mut Vec<fidl::Handle>| {
                if handles.len() > 0 {
                    failure::bail!("Expected no handles");
                }
                self.queue_send_raw_datagram(
                    connection_stream_id,
                    Some(&mut bytes),
                    false,
                    |peer, messages, bytes| {
                        peer.connect_to_service_sends += messages;
                        peer.connect_to_service_send_bytes += bytes;
                    },
                )
            },
        );
        match err {
            Ok(()) => Ok(stream_id),
            Err(e) => {
                if let Some(peer) = self.peers.get_mut(peer_id) {
                    peer.streams.remove(&id);
                }
                self.streams.remove(stream_id);
                Err(e)
            }
        }
    }

    /// Regenerate our description packet and send it to all peers.
    fn publish_node_description(&mut self, services: Vec<String>) -> Result<(), Error> {
        self.node_desc_packet = make_desc_packet(services)?;
        let stream_ids: Vec<StreamId<LinkData>> = self
            .peers
            .iter_mut()
            .map(|(_peer_id, peer)| peer)
            .filter(|peer| peer.is_client && peer.conn.0.is_established())
            .map(|peer| peer.connection_stream_id)
            .collect();
        for stream_id in stream_ids {
            let mut desc = self.node_desc_packet.clone();
            if let Err(e) = self.queue_send_raw_datagram(
                stream_id,
                Some(&mut desc),
                false,
                |peer, messages, bytes| {
                    peer.update_node_description_sends += messages;
                    peer.update_node_description_send_bytes += bytes;
                },
            ) {
                log::warn!("Failed to send datagram: {:?}", e);
            }
        }
        Ok(())
    }

    /// Send a datagram on a channel type stream.
    fn queue_send_channel_message(
        &mut self,
        stream_id: StreamId<LinkData>,
        bytes: Vec<u8>,
        handles: Vec<SendHandle>,
        update_stats: impl FnOnce(&mut Peer<LinkData>, u64, u64),
    ) -> Result<Vec<StreamId<LinkData>>, Error> {
        let stream = self
            .streams
            .get(stream_id)
            .ok_or_else(|| failure::format_err!("Stream not found {:?}", stream_id))?;
        let peer_id = stream.peer_id;
        let peer = self
            .peers
            .get_mut(peer_id)
            .ok_or_else(|| failure::format_err!("Peer not found {:?}", peer_id))?;
        let streams = &mut self.streams;
        let (handles, stream_ids): (Vec<_>, Vec<_>) = handles
            .into_iter()
            .map(|h| {
                let id = peer.next_stream << 2;
                peer.next_stream += 1;
                let (send, stream_type) = match h {
                    SendHandle::Channel => (
                        ZirconHandle::Channel(ChannelHandle { stream_id: zircon_stream_id(id) }),
                        StreamType::Channel,
                    ),
                };
                let stream_id = streams.insert(Stream {
                    peer_id,
                    id,
                    read_state: ReadState::new_bound(stream_type),
                });
                peer.streams.insert(id, stream_id);
                (send, stream_id)
            })
            .unzip();
        fidl::encoding::with_tls_encoded(
            &mut ZirconChannelMessage { bytes, handles },
            |bytes, handles| {
                if handles.len() != 0 {
                    failure::bail!("Unexpected handles in encoding");
                }
                self.queue_send_raw_datagram(stream_id, Some(bytes), false, update_stats)
            },
        )?;
        Ok(stream_ids)
    }

    /// Send a datagram on a stream.
    fn queue_send_raw_datagram(
        &mut self,
        stream_id: StreamId<LinkData>,
        frame: Option<&mut [u8]>,
        fin: bool,
        update_stats: impl FnOnce(&mut Peer<LinkData>, u64, u64),
    ) -> Result<(), Error> {
        log::trace!(
            "{:?} queue_send: stream_id={:?} frame={:?} fin={:?}",
            self.node_id,
            stream_id,
            frame,
            fin
        );
        let stream = self
            .streams
            .get(stream_id)
            .ok_or_else(|| failure::format_err!("Stream not found {:?}", stream_id))?;
        log::trace!("  peer_id={:?} stream_index={}", stream.peer_id, stream.id);
        let peer_id = stream.peer_id;
        let peer = self
            .peers
            .get_mut(peer_id)
            .ok_or_else(|| failure::format_err!("Peer not found {:?}", peer_id))?;
        log::trace!("  node_id={:?}", peer.node_id);
        if let Some(frame) = frame {
            let frame_len = frame.len();
            assert!(frame_len <= 0xffff_ffff);
            let header: [u8; 4] = [
                (frame_len & 0xff) as u8,
                ((frame_len >> 8) & 0xff) as u8,
                ((frame_len >> 16) & 0xff) as u8,
                ((frame_len >> 24) & 0xff) as u8,
            ];
            peer.conn
                .0
                .stream_send(stream.id, &header, false)
                .with_context(|_| format!("Sending to stream {:?} peer {:?}", stream, peer))?;
            peer.conn
                .0
                .stream_send(stream.id, frame, fin)
                .with_context(|_| format!("Sending to stream {:?} peer {:?}", stream, peer))?;
            update_stats(peer, 1u64, (header.len() + frame.len()) as u64);
        } else {
            peer.conn
                .0
                .stream_send(stream.id, &mut [], fin)
                .with_context(|_| format!("Sending to stream {:?} peer {:?}", stream, peer))?;
            update_stats(peer, 0u64, 0u64);
        }
        if !peer.pending_writes {
            log::trace!("Mark writable: {:?}", stream.peer_id);
            peer.pending_writes = true;
            self.write_peers.push(stream.peer_id);
        }
        if !peer.check_timeout {
            peer.check_timeout = true;
            self.check_timeouts.push(stream.peer_id);
        }
        Ok(())
    }

    fn server_config(&self) -> Result<quiche::Config, Error> {
        let mut config = quiche::Config::new(quiche::PROTOCOL_VERSION)
            .context("Creating quic configuration for server connection")?;
        let cert_file = self
            .server_cert_file
            .as_ref()
            .ok_or_else(|| failure::format_err!("No cert file for server"))?
            .as_ref()
            .as_ref()
            .to_str()
            .ok_or_else(|| failure::format_err!("Cannot convert path to string"))?;
        let key_file = self
            .server_key_file
            .as_ref()
            .ok_or_else(|| failure::format_err!("No key file for server"))?
            .as_ref()
            .as_ref()
            .to_str()
            .ok_or_else(|| failure::format_err!("Cannot convert path to string"))?;
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
        config.set_initial_max_streams_uni(0);
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
        config.set_initial_max_streams_uni(0);
        Ok(config)
    }

    /// Receive a packet from some link.
    fn queue_recv(
        &mut self,
        link_id: LinkId<LinkData>,
        routing_label: RoutingLabel,
        packet: &mut [u8],
    ) -> Result<(), Error> {
        let (peer_id, forward) = match (routing_label.to_client, routing_label.dst == self.node_id)
        {
            (false, true) => {
                let hdr = quiche::Header::from_slice(packet, quiche::MAX_CONN_ID_LEN)
                    .context("Decoding quic header")?;

                if hdr.ty == quiche::Type::VersionNegotiation {
                    failure::bail!("Version negotiation invalid on the server");
                }

                // If we're asked for a server connection, we should create a client connection
                self.ensure_client_peer_id(routing_label.src, Some(link_id))?;

                if let Some(server_peer_id) = self.server_peer_id(routing_label.src) {
                    (server_peer_id, false)
                } else if hdr.ty == quiche::Type::Initial {
                    let mut config = self.server_config()?;
                    let scid: Vec<u8> = rand::thread_rng()
                        .sample_iter(&rand::distributions::Standard)
                        .take(quiche::MAX_CONN_ID_LEN)
                        .collect();
                    let conn = PeerConn(
                        quiche::accept(&scid, None, &mut config)
                            .context("Creating quic server connection")?,
                    );
                    let connection_stream_id = self.streams.insert(Stream {
                        peer_id: PeerId::invalid(),
                        id: 0,
                        read_state: ReadState::new_bound(StreamType::PeerServerEnd),
                    });
                    let mut streams = BTreeMap::new();
                    streams.insert(0, connection_stream_id);
                    let peer_id = self.peers.insert(Peer {
                        conn,
                        next_stream: 1,
                        node_id: routing_label.src,
                        current_link: Some(link_id),
                        is_client: false,
                        pending_reads: false,
                        pending_writes: true,
                        check_timeout: false,
                        establishing: true,
                        timeout_generation: 0,
                        forward: Vec::new(),
                        streams,
                        connection_stream_id,
                        link_status_update_state: None,
                        messages_sent: 0,
                        bytes_sent: 0,
                        connect_to_service_sends: 0,
                        connect_to_service_send_bytes: 0,
                        update_node_description_sends: 0,
                        update_node_description_send_bytes: 0,
                        update_link_status_sends: 0,
                        update_link_status_send_bytes: 0,
                        update_link_status_ack_sends: 0,
                        update_link_status_ack_send_bytes: 0,
                    });
                    self.streams.get_mut(connection_stream_id).unwrap().peer_id = peer_id;
                    self.node_to_server_peer.insert(routing_label.src, peer_id);
                    self.write_peers.push(peer_id);
                    (peer_id, false)
                } else {
                    failure::bail!("No server for link {:?}, and not an Initial packet", link_id);
                }
            }
            (true, true) => (self.client_peer_id(routing_label.src, Some(link_id))?, false),
            (false, false) => {
                // If we're asked to forward a packet, we should have client connections to each end
                self.ensure_client_peer_id(routing_label.src, Some(link_id))?;
                self.ensure_client_peer_id(routing_label.dst, None)?;
                let peer_id = self.server_peer_id(routing_label.dst).ok_or_else(|| {
                    failure::format_err!("Node server peer not found for {:?}", routing_label.dst)
                })?;
                (peer_id, true)
            }
            (true, false) => {
                let peer_id = self.client_peer_id(routing_label.dst, Some(link_id))?;
                (peer_id, true)
            }
        };

        // Guaranteed correct ID by peer_index above.
        let peer = self.peers.get_mut(peer_id).unwrap();
        if forward {
            log::trace!("FORWARD: {:?}", routing_label);
            peer.forward.push((routing_label, packet.to_vec()));
        } else {
            peer.conn.0.recv(packet).context("Receiving packet on quic connection")?;
            if !peer.pending_reads {
                log::trace!("{:?} Mark readable: {:?}", self.node_id, peer_id);
                peer.pending_reads = true;
                self.read_peers.push(peer_id);
            }
            if peer.establishing && peer.conn.0.is_established() {
                peer.establishing = false;
                if peer.is_client {
                    self.newly_established_clients.push(peer.node_id);
                    self.need_to_send_node_description_clients.push(peer.connection_stream_id);
                }
            }
        }
        if !peer.pending_writes {
            log::trace!("{:?} Mark writable: {:?}", self.node_id, peer_id);
            peer.pending_writes = true;
            self.write_peers.push(peer_id);
        }
        if !peer.check_timeout {
            peer.check_timeout = true;
            self.check_timeouts.push(peer_id);
        }

        Ok(())
    }

    /// Process all received packets and make callbacks depending on what was contained in them.
    fn flush_recvs<Got>(&mut self, got: &mut Got)
    where
        Got: MessageReceiver<LinkData>,
    {
        let mut buf = [0_u8; MAX_RECV_LEN];

        while let Some(peer_id) = self.read_peers.pop() {
            if let Err(e) = self.recv_from_peer_id(peer_id, &mut buf, got) {
                log::warn!("Error receiving packet: {:?}", e);
                unimplemented!();
            }
        }

        while let Some(node_id) = self.newly_established_clients.pop() {
            got.established_connection(node_id);
        }
    }

    /// Helper for flush_recvs to process incoming packets from one peer.
    fn recv_from_peer_id<Got>(
        &mut self,
        peer_id: PeerId<LinkData>,
        buf: &mut [u8],
        got: &mut Got,
    ) -> Result<(), Error>
    where
        Got: MessageReceiver<LinkData>,
    {
        log::trace!("{:?} flush_recvs {:?}", self.node_id, peer_id);
        let mut peer = self
            .peers
            .get_mut(peer_id)
            .ok_or_else(|| failure::format_err!("Peer not found {:?}", peer_id))?;
        assert!(peer.pending_reads);
        peer.pending_reads = false;
        // TODO(ctiller): We currently copy out the readable streams here just to satisfy the
        // bounds checker. Find a way to avoid this wasteful allocation.
        let readable: Vec<u64> = peer.conn.0.readable().collect();
        for stream_index in readable {
            log::trace!("{:?}   stream {} is readable", self.node_id, stream_index);
            let mut stream_id = peer.streams.get(&stream_index).copied();
            if stream_id.is_none() {
                if !is_local_quic_stream_index(stream_index, peer.is_client) {
                    stream_id = Some(self.streams.insert(Stream {
                        peer_id,
                        id: stream_index,
                        read_state: ReadState::new_unbound(),
                    }));
                    peer.streams.insert(stream_index, stream_id.unwrap());
                } else {
                    // Getting here means a stream originating from this Overnet node was created
                    // without the creation of a peer stream: this is a bug, and this branch should
                    // never be reached.
                    unreachable!();
                }
            }
            let mut rs = self
                .streams
                .get_mut(stream_id.unwrap())
                .ok_or_else(|| failure::format_err!("Stream not found {:?}", stream_id))?
                .read_state
                .take();
            let remove = loop {
                match peer.conn.0.stream_recv(stream_index, buf) {
                    Ok((n, fin)) => {
                        let mut recv_message_context = RecvMessageContext {
                            node_id: self.node_id,
                            got,
                            peer_id,
                            peer: &mut peer,
                            stream_id: stream_id.unwrap(),
                            stream_index,
                            streams: &mut self.streams,
                            recently_mentioned_peers: &mut self.recently_mentioned_peers,
                            link_status_updates: &mut self.link_status_updates,
                            ack_link_status_updates: &mut self.ack_link_status_updates,
                        };
                        rs = rs.recv(
                            &mut buf[..n],
                            |stream_type: StreamType, message: Option<&mut [u8]>| {
                                recv_message_context.recv(stream_type, message)
                            },
                        );
                        if fin {
                            rs = rs.finished(|| {
                                recv_message_context.got.close(recv_message_context.stream_id)
                            });
                        }
                    }
                    Err(quiche::Error::Done) => break false,
                    _ => unimplemented!(),
                }
            };
            self.streams
                .get_mut(stream_id.unwrap())
                .ok_or_else(|| failure::format_err!("Stream not found {:?}", stream_id))?
                .read_state = rs;
            if remove {
                unimplemented!();
            }
        }
        Ok(())
    }

    /// Ensure that a client peer id exists for a given `node_id`
    /// If the client peer needs to be created, `link_id_hint` will be used as an interim hint
    /// as to the best route *to* this node, until a full routing update can be performed.
    fn ensure_client_peer_id(
        &mut self,
        node_id: NodeId,
        link_id_hint: Option<LinkId<LinkData>>,
    ) -> Result<(), Error> {
        self.client_peer_id(node_id, link_id_hint).map(|_| ())
    }

    /// Map a node id to the client peer id. Since we can always create a client peer, this will
    /// attempt to initiate a connection if there is no current connection.
    /// If the client peer needs to be created, `link_id_hint` will be used as an interim hint
    /// as to the best route *to* this node, until a full routing update can be performed.
    fn client_peer_id(
        &mut self,
        node_id: NodeId,
        link_id_hint: Option<LinkId<LinkData>>,
    ) -> Result<PeerId<LinkData>, Error> {
        if let Some(peer_id) = self.node_to_client_peer.get(&node_id) {
            log::trace!("Existing client: {:?}", node_id);
            return Ok(*peer_id);
        }

        log::trace!("New client: {:?}", node_id);

        let mut config = self.client_config()?;
        let scid: Vec<u8> = rand::thread_rng()
            .sample_iter(&rand::distributions::Standard)
            .take(quiche::MAX_CONN_ID_LEN)
            .collect();
        let conn = PeerConn(
            quiche::connect(None, &scid, &mut config).context("creating quic client connection")?,
        );
        let connection_stream_id = self.streams.insert(Stream {
            peer_id: PeerId::invalid(),
            id: 0,
            read_state: ReadState::new_bound(StreamType::PeerClientEnd),
        });
        let mut streams = BTreeMap::new();
        streams.insert(0, connection_stream_id);
        let peer_id = self.peers.insert(Peer {
            conn,
            next_stream: 1,
            node_id,
            current_link: link_id_hint,
            is_client: true,
            pending_reads: false,
            pending_writes: true,
            check_timeout: false,
            establishing: true,
            timeout_generation: 0,
            forward: Vec::new(),
            streams,
            connection_stream_id,
            link_status_update_state: Some(PeerLinkStatusUpdateState::Sent),
            messages_sent: 0,
            bytes_sent: 0,
            connect_to_service_sends: 0,
            connect_to_service_send_bytes: 0,
            update_node_description_sends: 0,
            update_node_description_send_bytes: 0,
            update_link_status_sends: 0,
            update_link_status_send_bytes: 0,
            update_link_status_ack_sends: 0,
            update_link_status_ack_send_bytes: 0,
        });
        self.streams.get_mut(connection_stream_id).unwrap().peer_id = peer_id;
        self.node_to_client_peer.insert(node_id, peer_id);
        self.write_peers.push(peer_id);
        Ok(peer_id)
    }

    /// Retrieve an existing server peer id for some node id.
    fn server_peer_id(&mut self, node_id: NodeId) -> Option<PeerId<LinkData>> {
        self.node_to_server_peer.get(&node_id).copied()
    }

    /// Update timers.
    fn update_time(&mut self, tm: Time) {
        self.now = tm;
        while let Some(Timeout(when, peer_id, timeout_generation)) = self.queued_timeouts.pop() {
            if tm < when {
                self.queued_timeouts.push(Timeout(when, peer_id, timeout_generation));
                break;
            }
            if let Some(peer) = self.peers.get_mut(peer_id) {
                if peer.timeout_generation != timeout_generation {
                    continue;
                }
                log::trace!(
                    "{:?} expire peer timeout {:?} gen={}",
                    self.node_id,
                    peer_id,
                    timeout_generation
                );
                peer.conn.0.on_timeout();
                if !peer.check_timeout {
                    peer.check_timeout = true;
                    self.check_timeouts.push(peer_id);
                }
                if !peer.pending_writes {
                    peer.pending_writes = true;
                    self.write_peers.push(peer_id);
                }
                if peer.establishing && peer.conn.0.is_established() {
                    peer.establishing = false;
                    if peer.is_client {
                        self.newly_established_clients.push(peer.node_id);
                        self.need_to_send_node_description_clients.push(peer.connection_stream_id);
                    }
                }
            }
        }
    }

    /// Calculate when the next timeout should be triggered.
    fn next_timeout(&mut self) -> Option<Time> {
        while let Some(peer_id) = self.check_timeouts.pop() {
            if let Some(peer) = self.peers.get_mut(peer_id) {
                assert!(peer.check_timeout);
                peer.check_timeout = false;
                peer.timeout_generation += 1;
                if let Some(duration) = peer.conn.0.timeout() {
                    let when = Time::after(self.now, duration.into());
                    log::trace!(
                        "{:?} queue timeout in {:?} in {:?} [gen={}]",
                        self.node_id,
                        peer_id,
                        duration,
                        peer.timeout_generation
                    );
                    self.queued_timeouts.push(Timeout(when, peer_id, peer.timeout_generation));
                }
            }
        }
        self.queued_timeouts.peek().map(|Timeout(when, _, _)| *when)
    }
}

/// Maximum length of a received packet from quic.
const MAX_RECV_LEN: usize = 1500;

/// Helper to receive one message.
/// Borrows of all relevant data structures from Endpoints (but only them).
struct RecvMessageContext<'a, Got, LinkData: Copy + Debug> {
    node_id: NodeId,
    got: &'a mut Got,
    peer_id: PeerId<LinkData>,
    peer: &'a mut Peer<LinkData>,
    stream_index: u64,
    stream_id: StreamId<LinkData>,
    streams: &'a mut SaltSlab<Stream<LinkData>>,
    recently_mentioned_peers: &'a mut Vec<(NodeId, Option<LinkId<LinkData>>)>,
    link_status_updates: &'a mut BinaryHeap<StreamId<LinkData>>,
    ack_link_status_updates: &'a mut BinaryHeap<StreamId<LinkData>>,
}

impl<'a, Got, LinkData: Copy + Debug> RecvMessageContext<'a, Got, LinkData>
where
    Got: MessageReceiver<LinkData>,
{
    /// Receive a datagram, parse it, and dispatch it to the right methods.
    /// message==None => end of stream.
    fn recv(&mut self, stream_type: StreamType, message: Option<&mut [u8]>) -> Result<(), Error> {
        log::trace!(
            "{:?} Peer {:?} {:?} stream {:?} index {} gets {:?}",
            self.node_id,
            self.peer_id,
            self.peer.node_id,
            self.stream_id,
            self.stream_index,
            message
        );

        match (stream_type, message) {
            (StreamType::PeerClientEnd, None) => {
                failure::bail!("Peer control stream closed on client")
            }
            (StreamType::PeerServerEnd, None) => {
                failure::bail!("Peer control stream closed on server")
            }
            (_, None) => {
                self.got.close(self.stream_id);
                Ok(())
            }
            (StreamType::Channel, Some(bytes)) => {
                let mut msg = decode_fidl::<ZirconChannelMessage>(bytes)?;
                let mut handles = Vec::new();
                for unbound in msg.handles.into_iter() {
                    let bound = match unbound {
                        ZirconHandle::Channel(ChannelHandle { stream_id: stream_index }) => self
                            .bind_stream(
                                StreamType::Channel,
                                stream_index.id,
                                |stream_id, got| got.bind_channel(stream_id),
                            )?,
                        ZirconHandle::Socket(_) => unimplemented!(),
                    };
                    handles.push(bound);
                }
                self.got.channel_recv(self.stream_id, &mut msg.bytes, &mut handles)
            }
            (StreamType::PeerServerEnd, Some(bytes)) => {
                let msg = decode_fidl::<PeerMessage>(bytes).context("Decoding PeerMessage")?;
                log::trace!("{:?} Got peer message: {:?}", self.node_id, msg);
                match msg {
                    PeerMessage::ConnectToService(ConnectToService {
                        service_name: service,
                        stream_id: new_stream_index,
                        options: _,
                    }) => {
                        log::trace!(
                            "Log new connection request: peer {:?} new_stream_index {:?} service: {}",
                            self.peer_id,
                            new_stream_index,
                            service
                        );
                        let peer_node_id = self.peer.node_id;
                        self.bind_stream(StreamType::Channel, new_stream_index, |stream_id, got| {
                            got.connect_channel(
                                stream_id,
                                &service,
                                fidl_fuchsia_overnet::ConnectionInfo {
                                    peer: Some(peer_node_id.into()),
                                },
                            )
                        })
                    }
                    PeerMessage::UpdateNodeDescription(PeerDescription { services }) => {
                        self.got.update_node(
                            self.peer.node_id,
                            NodeDescription { services: services.unwrap_or(vec![]) },
                        );
                        Ok(())
                    }
                    PeerMessage::UpdateLinkStatus(UpdateLinkStatus { link_status }) => {
                        self.ack_link_status_updates.push(self.stream_id);
                        for LinkStatus { to, local_id, metrics, .. } in link_status {
                            let to: NodeId = to.id.into();
                            self.recently_mentioned_peers.push((to, self.peer.current_link));
                            self.got.update_link(
                                self.peer.node_id,
                                to,
                                local_id.into(),
                                LinkDescription {
                                    round_trip_time: std::time::Duration::from_micros(
                                        metrics.round_trip_time.unwrap_or(std::u64::MAX),
                                    ),
                                },
                            );
                        }
                        Ok(())
                    }
                    x => {
                        failure::bail!("Unknown variant: {:?}", x);
                    }
                }
            }
            (StreamType::PeerClientEnd, Some(bytes)) => {
                let msg = decode_fidl::<PeerReply>(bytes).context("Decoding PeerReply")?;
                log::trace!("{:?} Got peer reply: {:?}", self.node_id, msg);
                match msg {
                    PeerReply::UpdateLinkStatusAck { .. } => {
                        let new_status = match self.peer.link_status_update_state {
                            Some(PeerLinkStatusUpdateState::Unscheduled) => {
                                failure::bail!("Unexpected unscheduled status")
                            }
                            Some(PeerLinkStatusUpdateState::Sent) => {
                                Some(PeerLinkStatusUpdateState::Unscheduled)
                            }
                            Some(PeerLinkStatusUpdateState::SentOutdated) => {
                                self.link_status_updates.push(self.stream_id);
                                Some(PeerLinkStatusUpdateState::Sent)
                            }
                            None => unreachable!(),
                        };
                        self.peer.link_status_update_state = new_status;
                        Ok(())
                    }
                    x => {
                        failure::bail!("Unknown variant: {:?}", x);
                    }
                }
            }
        }
    }

    /// Bind a new stream that was received in some packet.
    fn bind_stream<R>(
        &mut self,
        stream_type: StreamType,
        stream_index: u64,
        mut app_bind: impl FnMut(StreamId<LinkData>, &mut Got) -> Result<R, Error>,
    ) -> Result<R, Error> {
        if stream_index == 0 {
            failure::bail!("Cannot connect stream 0");
        }
        let bind_stream_id = match self.peer.streams.entry(stream_index) {
            btree_map::Entry::Occupied(stream_id) => *stream_id.get(),
            btree_map::Entry::Vacant(v) => {
                let stream_id = *v.insert(self.streams.insert(Stream {
                    peer_id: self.peer_id,
                    id: stream_index,
                    read_state: ReadState::new_bound(stream_type),
                }));
                log::trace!("Bind stream stream_index={} no current read_state", stream_index);
                // early return
                return app_bind(stream_id, self.got);
            }
        };
        let result = app_bind(bind_stream_id, self.got)?;
        let mut rs = self
            .streams
            .get_mut(bind_stream_id)
            .ok_or_else(|| failure::format_err!("Stream not found for bind {:?}", bind_stream_id))?
            .read_state
            .take();
        log::trace!("Bind stream stream_index={} read_state={:?}", stream_index, rs);
        let mut recv_message_context = RecvMessageContext {
            node_id: self.node_id,
            got: self.got,
            peer_id: self.peer_id,
            peer: self.peer,
            stream_id: bind_stream_id,
            stream_index,
            streams: self.streams,
            recently_mentioned_peers: self.recently_mentioned_peers,
            link_status_updates: self.link_status_updates,
            ack_link_status_updates: self.ack_link_status_updates,
        };
        rs = rs.bind(stream_type, |stream_type: StreamType, message: Option<&mut [u8]>| {
            recv_message_context.recv(stream_type, message)
        });
        self.streams
            .get_mut(bind_stream_id)
            .ok_or_else(|| failure::format_err!("Stream not found for bind {:?}", bind_stream_id))?
            .read_state = rs;
        Ok(result)
    }
}

struct Links<LinkData: Copy + Debug, Time: RouterTime> {
    /// Node id of the router
    node_id: NodeId,
    /// All known links
    links: SaltSlab<Link<LinkData>>,
    /// Queues of things that need to happen to links
    queues: LinkQueues<LinkData, Time>,
}

struct LinkQueues<LinkData: Copy + Debug, Time: RouterTime> {
    /// Links that need pings
    ping_links: BinaryHeap<LinkId<LinkData>>,
    /// Links that need pongs
    pong_links: BinaryHeap<LinkId<LinkData>>,
    /// Timeouts that are pending
    queued_timeouts: BinaryHeap<Timeout<Time, LinkId<LinkData>>>,
    /// Are there link updates to send out?
    send_link_updates: bool,
    /// Which links were updated locally?
    link_updates: BTreeSet<LinkId<LinkData>>,
}

impl<LinkData: Copy + Debug, Time: RouterTime> LinkQueues<LinkData, Time> {
    /// Handle a PingTrackerResult
    fn handle_ping_tracker_result(
        &mut self,
        link_id: LinkId<LinkData>,
        link: &mut Link<LinkData>,
        ptres: PingTrackerResult,
    ) {
        log::trace!("HANDLE_PTRES: link_id:{:?} ptres:{:?}", link_id, ptres);
        if ptres.sched_send {
            self.ping_links.push(link_id);
        }
        if let Some(dt) = ptres.sched_timeout {
            link.timeout_generation += 1;
            self.queued_timeouts.push(Timeout(
                Time::after(Time::now(), dt.into()),
                link_id,
                link.timeout_generation,
            ));
        }
        if ptres.new_round_trip_time {
            self.link_updates.insert(link_id);
            self.send_link_updates = true;
        }
    }
}

impl<LinkData: Copy + Debug, Time: RouterTime> Links<LinkData, Time> {
    fn recv_packet<'a>(
        &mut self,
        link_id: LinkId<LinkData>,
        packet: &'a mut [u8],
    ) -> Result<(RoutingLabel, &'a mut [u8]), Error> {
        let link =
            self.links.get_mut(link_id).ok_or_else(|| failure::format_err!("Link not found"))?;
        log::trace!(
            "Decode routing label from id={:?}[peer={:?}, id={:?}, up={:?}]",
            link_id,
            link.peer,
            link.id,
            link.link_data
        );
        link.received_packets += 1;
        link.received_bytes += packet.len() as u64;
        let (routing_label, packet_length) = RoutingLabel::decode(link.peer, self.node_id, packet)?;
        log::trace!(
            "{:?} routing_label={:?} packet_length={} src_len={}",
            self.node_id,
            routing_label,
            packet_length,
            packet.len()
        );
        let packet = &mut packet[..packet_length];

        if let Some(ping) = routing_label.ping {
            if link.pong_tracker.got_ping(ping) {
                self.queues.pong_links.push(link_id);
            }
        }

        if let Some(pong) = routing_label.pong {
            let r = link.ping_tracker.got_pong(Instant::now(), pong);
            self.queues.handle_ping_tracker_result(link_id, link, r);
        }

        Ok((routing_label, packet))
    }

    /// Flush outstanding packets to links, via `send_to`
    fn flush_sends<SendTo>(&mut self, mut send_to: SendTo, buf: &mut [u8])
    where
        SendTo: FnMut(&LinkData, &mut [u8]) -> Result<(), Error>,
    {
        while let Some(link_id) = self.queues.pong_links.pop() {
            if let Err(e) = self.pong_link(&mut send_to, link_id, buf) {
                log::warn!("Pong link failed: {:?}", e);
            }
        }

        while let Some(link_id) = self.queues.ping_links.pop() {
            if let Err(e) = self.ping_link(&mut send_to, link_id, buf) {
                log::warn!("Ping link failed: {:?}", e);
            }
        }
    }

    /// Flush pongs that haven't yet been sent by flush_sends_to_peer
    fn pong_link<SendTo>(
        &mut self,
        send_to: &mut SendTo,
        link_id: LinkId<LinkData>,
        buf: &mut [u8],
    ) -> Result<(), Error>
    where
        SendTo: FnMut(&LinkData, &mut [u8]) -> Result<(), Error>,
    {
        let link = self
            .links
            .get_mut(link_id)
            .ok_or_else(|| failure::format_err!("Link {:?} expired before pong", link_id))?;
        let src = self.node_id;
        let dst = link.peer;
        if let Some(id) = link.pong_tracker.maybe_send_pong() {
            let (ping, r) = link.ping_tracker.maybe_send_ping(Instant::now(), false);
            self.queues.handle_ping_tracker_result(link_id, link, r);
            if ping.is_some() {
                link.pings_sent += 1;
            }
            let len = RoutingLabel {
                src,
                dst,
                ping,
                pong: Some(id),
                to_client: false,
                debug_token: crate::labels::new_debug_token(),
            }
            .encode_for_link(src, dst, &mut buf[..])?;
            link.sent_packets += 1;
            link.sent_bytes += len as u64;
            send_to(&link.link_data, &mut buf[..len])?;
        }
        Ok(())
    }

    /// Flush pings that haven't yet been sent by flush_sends_to_peer NOR ping_link
    fn ping_link<SendTo>(
        &mut self,
        send_to: &mut SendTo,
        link_id: LinkId<LinkData>,
        buf: &mut [u8],
    ) -> Result<(), Error>
    where
        SendTo: FnMut(&LinkData, &mut [u8]) -> Result<(), Error>,
    {
        let link = self
            .links
            .get_mut(link_id)
            .ok_or_else(|| failure::format_err!("Link {:?} expired before pong", link_id))?;
        let src = self.node_id;
        let dst = link.peer;
        let (ping, r) = link.ping_tracker.maybe_send_ping(Instant::now(), true);
        self.queues.handle_ping_tracker_result(link_id, link, r);
        if ping.is_some() {
            let len = RoutingLabel {
                src,
                dst,
                ping,
                pong: link.pong_tracker.maybe_send_pong(),
                to_client: false,
                debug_token: crate::labels::new_debug_token(),
            }
            .encode_for_link(src, dst, &mut buf[..])?;
            link.sent_packets += 1;
            link.sent_bytes += len as u64;
            link.pings_sent += 1;
            send_to(&link.link_data, &mut buf[..len])?;
        }
        Ok(())
    }

    /// Update timers.
    fn update_time(&mut self, tm: Time) {
        while let Some(Timeout(when, link_id, timeout_generation)) =
            self.queues.queued_timeouts.pop()
        {
            if tm < when {
                self.queues.queued_timeouts.push(Timeout(when, link_id, timeout_generation));
                break;
            }
            if let Some(link) = self.links.get_mut(link_id) {
                if link.timeout_generation != timeout_generation {
                    continue;
                }
                log::trace!(
                    "{:?} expire link timeout {:?} gen={}",
                    self.node_id,
                    link_id,
                    timeout_generation
                );
                let r = link.ping_tracker.on_timeout(Instant::now());
                self.queues.handle_ping_tracker_result(link_id, link, r);
            }
        }
    }
}

/// Router maintains global state for one node_id.
/// `LinkData` is a token identifying a link for layers above Router.
/// `Time` is a representation of time for the Router, to assist injecting different platforms
/// schemes.
pub struct Router<LinkData: Copy + Debug, Time: RouterTime> {
    /// Our node id
    node_id: NodeId,
    /// Endpoint data-structure
    endpoints: Endpoints<LinkData, Time>,
    /// Links data-structure
    links: Links<LinkData, Time>,
    /// Ack link status frame
    ack_link_status_frame: Vec<u8>,
}

fn make_desc_packet(services: Vec<String>) -> Result<Vec<u8>, Error> {
    fidl::encoding::with_tls_encoded(
        &mut PeerMessage::UpdateNodeDescription(PeerDescription { services: Some(services) }),
        |bytes, handles| {
            if handles.len() != 0 {
                failure::bail!("Unexpected handles in encoding");
            }
            Ok(bytes.clone())
        },
    )
}

/// Generate a new random node id
pub fn generate_node_id() -> NodeId {
    rand::thread_rng().gen::<u64>().into()
}

impl<LinkData: Copy + Debug, Time: RouterTime> Router<LinkData, Time> {
    /// New with some set of options
    pub fn new_with_options(options: RouterOptions) -> Self {
        let node_id = options.node_id.unwrap_or_else(generate_node_id);
        let mut ack_link_status_frame = Vec::new();
        fidl::encoding::Encoder::encode(
            &mut ack_link_status_frame,
            &mut Vec::new(),
            &mut PeerReply::UpdateLinkStatusAck(fidl_fuchsia_overnet_protocol::Empty {}),
        )
        .unwrap();
        Router {
            node_id,
            endpoints: Endpoints {
                node_id,
                peers: SaltSlab::new(),
                streams: SaltSlab::new(),
                node_to_client_peer: BTreeMap::new(),
                node_to_server_peer: BTreeMap::new(),
                write_peers: BinaryHeap::new(),
                read_peers: BinaryHeap::new(),
                check_timeouts: BinaryHeap::new(),
                queued_timeouts: BinaryHeap::new(),
                newly_established_clients: BinaryHeap::new(),
                need_to_send_node_description_clients: BinaryHeap::new(),
                recently_mentioned_peers: Vec::new(),
                node_desc_packet: make_desc_packet(vec![]).unwrap(),
                now: Time::now(),
                server_cert_file: options.quic_server_cert_file,
                server_key_file: options.quic_server_key_file,
                link_status_updates: BinaryHeap::new(),
                ack_link_status_updates: BinaryHeap::new(),
            },
            links: Links {
                node_id,
                links: SaltSlab::new(),
                queues: LinkQueues {
                    ping_links: BinaryHeap::new(),
                    pong_links: BinaryHeap::new(),
                    queued_timeouts: BinaryHeap::new(),
                    send_link_updates: false,
                    link_updates: BTreeSet::new(),
                },
            },
            ack_link_status_frame,
        }
    }

    /// New object with default options
    pub fn new() -> Self {
        Self::new_with_options(RouterOptions::new())
    }

    /// Return the key for the SaltSlab representing streams
    pub fn shadow_streams<T>(&self) -> ShadowSlab<Stream<LinkData>, T> {
        self.endpoints.streams.shadow()
    }

    /// Create a new link to some node, returning a `LinkId` describing it.
    pub fn new_link(
        &mut self,
        peer: NodeId,
        node_link_id: NodeLinkId,
        link_data: LinkData,
    ) -> Result<LinkId<LinkData>, Error> {
        let (ping_tracker, ptres) = PingTracker::new();
        let link_id = self.links.links.insert(Link {
            peer,
            id: node_link_id,
            link_data,
            ping_tracker,
            pong_tracker: PongTracker::new(),
            timeout_generation: 1,
            sent_packets: 0,
            sent_bytes: 0,
            received_bytes: 0,
            received_packets: 0,
            pings_sent: 0,
            packets_forwarded: 0,
        });
        log::trace!(
            "new_link: id={:?} peer={:?} node_link_id={:?} link_data={:?}",
            link_id,
            peer,
            node_link_id,
            link_data
        );
        let client_peer = self.endpoints.client_peer_id(peer, Some(link_id))?;
        let server_peer = self.endpoints.server_peer_id(peer);
        self.links.queues.handle_ping_tracker_result(
            link_id,
            self.links.links.get_mut(link_id).unwrap(),
            ptres,
        );
        if let Some(peer) = self.endpoints.peers.get_mut(client_peer) {
            if peer.current_link.is_none() {
                peer.current_link = Some(link_id);
                if !peer.pending_writes {
                    log::trace!("{:?} Mark writable: {:?}", self.node_id, client_peer);
                    peer.pending_writes = true;
                    self.endpoints.write_peers.push(client_peer);
                }
            }
        }
        if let Some(server_peer) = server_peer {
            if let Some(peer) = self.endpoints.peers.get_mut(server_peer) {
                if peer.current_link.is_none() {
                    peer.current_link = Some(link_id);
                    if !peer.pending_writes {
                        log::trace!("{:?} Mark writable: {:?}", self.node_id, server_peer);
                        peer.pending_writes = true;
                        self.endpoints.write_peers.push(server_peer);
                    }
                }
            }
        }
        log::trace!("LINK TABLE: {:?}", self.links.links);
        Ok(link_id)
    }

    /// Accessor for the node id of this router.
    pub fn node_id(&self) -> NodeId {
        self.node_id
    }

    /// Drop a link that is no longer needed.
    pub fn drop_link(&mut self, link_id: LinkId<LinkData>) {
        self.links.links.remove(link_id);
    }

    /// Create a new stream to advertised service `service` on remote node id `node`.
    pub fn new_stream(&mut self, node: NodeId, service: &str) -> Result<StreamId<LinkData>, Error> {
        self.endpoints.new_stream(node, service)
    }

    /// Update the routing table for `dest` to use `link_id`.
    pub fn adjust_route(&mut self, dest: NodeId, link_id: LinkId<LinkData>) -> Result<(), Error> {
        self.endpoints.adjust_route(dest, link_id)
    }

    /// Return true if there's a client oriented established connection to `dest`.
    pub fn connected_to(&self, dest: NodeId) -> bool {
        self.endpoints.connected_to(dest)
    }

    /// Regenerate our description packet and send it to all peers.
    pub fn publish_node_description(&mut self, services: Vec<String>) -> Result<(), Error> {
        self.endpoints.publish_node_description(services)
    }

    /// Send a datagram on a channel type stream.
    pub fn queue_send_channel_message(
        &mut self,
        stream_id: StreamId<LinkData>,
        bytes: Vec<u8>,
        handles: Vec<SendHandle>,
    ) -> Result<Vec<StreamId<LinkData>>, Error> {
        self.endpoints.queue_send_channel_message(
            stream_id,
            bytes,
            handles,
            |peer, messages, bytes| {
                peer.messages_sent += messages;
                peer.bytes_sent += bytes;
            },
        )
    }

    /// Send a datagram on a stream.
    pub fn queue_send_raw_datagram(
        &mut self,
        stream_id: StreamId<LinkData>,
        frame: Option<&mut [u8]>,
        fin: bool,
    ) -> Result<(), Error> {
        self.endpoints.queue_send_raw_datagram(stream_id, frame, fin, |peer, messages, bytes| {
            peer.messages_sent += messages;
            peer.bytes_sent += bytes;
        })
    }

    /// Receive a packet from some link.
    pub fn queue_recv(&mut self, link_id: LinkId<LinkData>, packet: &mut [u8]) {
        if packet.len() < 1 {
            return;
        }

        match self.links.recv_packet(link_id, packet) {
            Ok((routing_label, packet)) => {
                if packet.len() > 0 {
                    if let Err(e) = self.endpoints.queue_recv(link_id, routing_label, packet) {
                        log::warn!("Error receiving packet from link {:?}: {:?}", link_id, e);
                    }
                }
            }
            Err(e) => {
                log::warn!("Error routing packet from link {:?}: {:?}", link_id, e);
            }
        }
    }

    /// Flush outstanding packets destined to one peer to links, via `send_to`
    fn flush_sends_to_peer<SendTo>(
        &mut self,
        send_to: &mut SendTo,
        peer_id: PeerId<LinkData>,
        buf: &mut [u8],
    ) -> Result<(), Error>
    where
        SendTo: FnMut(&LinkData, &mut [u8]) -> Result<(), Error>,
    {
        let buf_len = buf.len();
        log::trace!("{:?} flush_sends {:?}", self.node_id, peer_id);
        let peer = self
            .endpoints
            .peers
            .get_mut(peer_id)
            .ok_or_else(|| failure::format_err!("Peer {:?} not found for sending", peer_id))?;
        assert!(peer.pending_writes);
        peer.pending_writes = false;
        let current_link = peer.current_link.ok_or_else(|| {
            failure::format_err!(
                "No current link for peer {:?}; node={:?} client={}",
                peer_id,
                peer.node_id,
                peer.is_client
            )
        })?;
        let link = self.links.links.get_mut(current_link).ok_or_else(|| {
            failure::format_err!("Current link {:?} for peer {:?} not found", current_link, peer_id)
        })?;
        let peer_node_id = peer.node_id;
        let link_dst_id = link.peer;
        let is_peer_client = peer.is_client;
        let src_node_id = self.node_id;
        for forward in peer.forward.drain(..) {
            let mut packet = forward.1;
            forward.0.encode_for_link(src_node_id, link_dst_id, &mut packet)?;
            link.sent_packets += 1;
            link.sent_bytes += packet.len() as u64;
            link.packets_forwarded += 1;
            send_to(&link.link_data, packet.as_mut_slice())?;
        }
        loop {
            match peer.conn.0.send(&mut buf[..buf_len - MAX_ROUTING_LABEL_LENGTH]) {
                Ok(n) => {
                    let (ping, r) = link.ping_tracker.maybe_send_ping(Instant::now(), false);
                    if ping.is_some() {
                        link.pings_sent += 1;
                    }
                    self.links.queues.handle_ping_tracker_result(current_link, link, r);
                    let rl = RoutingLabel {
                        src: src_node_id,
                        dst: peer_node_id,
                        ping,
                        pong: link.pong_tracker.maybe_send_pong(),
                        to_client: !is_peer_client,
                        debug_token: crate::labels::new_debug_token(),
                    };
                    log::trace!("outgoing routing label {:?}", rl);
                    let suffix_len = rl.encode_for_link(src_node_id, link_dst_id, &mut buf[n..])?;
                    if !peer.check_timeout {
                        peer.check_timeout = true;
                        self.endpoints.check_timeouts.push(peer_id);
                    }
                    if peer.establishing && peer.conn.0.is_established() {
                        peer.establishing = false;
                        if peer.is_client {
                            self.endpoints.newly_established_clients.push(peer.node_id);
                            self.endpoints
                                .need_to_send_node_description_clients
                                .push(peer.connection_stream_id);
                        }
                    }
                    log::trace!(
                        "send on link: {:?}[peer={:?}, id={:?}, up={:?}]",
                        current_link,
                        link.peer,
                        link.id,
                        link.link_data
                    );
                    let packet_len = n + suffix_len;
                    link.sent_packets += 1;
                    link.sent_bytes += packet_len as u64;
                    send_to(&link.link_data, &mut buf[..packet_len])?;
                }
                Err(quiche::Error::Done) => {
                    if peer.establishing && peer.conn.0.is_established() {
                        peer.establishing = false;
                        if peer.is_client {
                            self.endpoints.newly_established_clients.push(peer.node_id);
                            self.endpoints
                                .need_to_send_node_description_clients
                                .push(peer.connection_stream_id);
                        }
                    }
                    log::trace!("{:?} sends done {:?}", self.node_id, peer_id);
                    return Ok(());
                }
                _ => unimplemented!(),
            }
        }
    }

    /// Flush outstanding packets to links, via `send_to`
    pub fn flush_sends<SendTo>(&mut self, mut send_to: SendTo)
    where
        SendTo: FnMut(&LinkData, &mut [u8]) -> Result<(), Error>,
    {
        self.flush_internal_sends();

        const MAX_LEN: usize = 1500;
        let mut buf = [0_u8; MAX_LEN];

        while let Some(peer_id) = self.endpoints.write_peers.pop() {
            if let Err(e) = self.flush_sends_to_peer(&mut send_to, peer_id, &mut buf) {
                log::warn!("Send to peer failed: {:?}", e);
                if let Some(peer) = self.endpoints.peers.get_mut(peer_id) {
                    if let Some(link_id) = peer.current_link.take() {
                        self.drop_link(link_id);
                    }
                }
            }
        }

        self.links.flush_sends(&mut send_to, &mut buf);
    }

    /// Write out any sends that have been queued by us (avoids some borrow checker problems).
    fn flush_internal_sends(&mut self) {
        while let Some((peer, link_id_hint)) = self.endpoints.recently_mentioned_peers.pop() {
            if let Err(e) = self.endpoints.ensure_client_peer_id(peer, link_id_hint) {
                log::warn!("Failed ensuring recently seen peer id has a client endpoint: {}", e);
            }
        }

        if self.links.queues.send_link_updates {
            self.links.queues.send_link_updates = false;
            for (_peer_id, peer) in self.endpoints.peers.iter_mut() {
                let new_status = match peer.link_status_update_state {
                    Some(PeerLinkStatusUpdateState::Unscheduled) => {
                        self.endpoints.link_status_updates.push(peer.connection_stream_id);
                        Some(PeerLinkStatusUpdateState::Sent)
                    }
                    Some(PeerLinkStatusUpdateState::Sent) => {
                        Some(PeerLinkStatusUpdateState::SentOutdated)
                    }
                    Some(PeerLinkStatusUpdateState::SentOutdated) => {
                        Some(PeerLinkStatusUpdateState::SentOutdated)
                    }
                    None => None,
                };
                peer.link_status_update_state = new_status;
            }
        }

        while let Some(stream_id) = self.endpoints.need_to_send_node_description_clients.pop() {
            if let Err(e) = self.endpoints.queue_send_raw_datagram(
                stream_id,
                Some(&mut self.endpoints.node_desc_packet.clone()),
                false,
                |peer, messages, bytes| {
                    peer.update_node_description_sends += messages;
                    peer.update_node_description_send_bytes += bytes;
                },
            ) {
                log::warn!("Failed to send initial update to {:?}: {:?}", stream_id, e);
            }
            self.endpoints.link_status_updates.push(stream_id);
        }

        while let Some(stream_id) = self.endpoints.ack_link_status_updates.pop() {
            if let Err(e) = self.endpoints.queue_send_raw_datagram(
                stream_id,
                Some(&mut self.ack_link_status_frame.clone()),
                false,
                |peer, messages, bytes| {
                    peer.update_link_status_ack_sends += messages;
                    peer.update_link_status_ack_send_bytes += bytes;
                },
            ) {
                log::warn!("Failed to ack link state update from {:?}: {:?}", stream_id, e);
            }
        }

        if !self.endpoints.link_status_updates.is_empty() {
            let mut status = PeerMessage::UpdateLinkStatus(UpdateLinkStatus {
                link_status: self
                    .links
                    .links
                    .iter()
                    .filter_map(|(_, link)| link.make_status())
                    .collect(),
            });
            fidl::encoding::with_tls_coding_bufs(|bytes, handles| {
                if let Err(e) = fidl::encoding::Encoder::encode(bytes, handles, &mut status) {
                    log::warn!("{}", e);
                    return;
                }
                let mut last_sent = None;
                while let Some(stream_id) = self.endpoints.link_status_updates.pop() {
                    if last_sent == Some(stream_id) {
                        continue;
                    }
                    last_sent = Some(stream_id);
                    if let Err(e) = self.endpoints.queue_send_raw_datagram(
                        stream_id,
                        Some(&mut bytes.clone()),
                        false,
                        |peer, messages, bytes| {
                            peer.update_link_status_sends += messages;
                            peer.update_link_status_send_bytes += bytes;
                        },
                    ) {
                        log::warn!("Failed to send link status update to {:?}: {:?}", stream_id, e);
                    }
                }
            });
        }
    }

    /// Process all received packets and make callbacks depending on what was contained in them.
    pub fn flush_recvs<Got>(&mut self, got: &mut Got)
    where
        Got: MessageReceiver<LinkData>,
    {
        self.endpoints.flush_recvs(got);

        // Will be drained in flush_internal_sends
        for link_id in self.links.queues.link_updates.iter() {
            if let Some(link) = self.links.links.get(*link_id) {
                if let Some(desc) = link.make_desc() {
                    got.update_link(self.node_id, link.peer, link.id, desc);
                }
            }
        }
    }

    /// Update timers.
    pub fn update_time(&mut self, tm: Time) {
        self.links.update_time(tm);
        self.endpoints.update_time(tm)
    }

    /// Calculate when the next timeout should be triggered.
    pub fn next_timeout(&mut self) -> Option<Time> {
        self.endpoints.next_timeout()
    }

    /// Diagnostic information for links
    pub fn link_diagnostics(&self) -> Vec<LinkDiagnosticInfo> {
        self.links
            .links
            .iter()
            .map(|(_, link)| LinkDiagnosticInfo {
                source: Some(self.node_id.into()),
                destination: Some(link.peer.into()),
                source_local_id: Some(link.id.0),
                sent_packets: Some(link.sent_packets),
                received_packets: Some(link.received_packets),
                sent_bytes: Some(link.sent_bytes),
                received_bytes: Some(link.received_bytes),
                pings_sent: Some(link.pings_sent),
                packets_forwarded: Some(link.packets_forwarded),
                round_trip_time_microseconds: link
                    .ping_tracker
                    .round_trip_time()
                    .map(|rtt| rtt.as_micros().try_into().unwrap_or(std::u64::MAX)),
            })
            .collect()
    }

    /// Diagnostic information for peer connections
    pub fn peer_diagnostics(&self) -> Vec<PeerConnectionDiagnosticInfo> {
        self.endpoints
            .peers
            .iter()
            .map(|(_, peer)| {
                let conn = &peer.conn.0;
                let stats = conn.stats();
                PeerConnectionDiagnosticInfo {
                    source: Some(self.node_id.into()),
                    destination: Some(peer.node_id.into()),
                    is_client: Some(peer.is_client),
                    is_established: Some(conn.is_established()),
                    received_packets: Some(stats.recv as u64),
                    sent_packets: Some(stats.sent as u64),
                    lost_packets: Some(stats.lost as u64),
                    messages_sent: Some(peer.messages_sent),
                    bytes_sent: Some(peer.bytes_sent),
                    connect_to_service_sends: Some(peer.connect_to_service_sends),
                    connect_to_service_send_bytes: Some(peer.connect_to_service_send_bytes),
                    update_node_description_sends: Some(peer.update_node_description_sends),
                    update_node_description_send_bytes: Some(
                        peer.update_node_description_send_bytes,
                    ),
                    update_link_status_sends: Some(peer.update_link_status_sends),
                    update_link_status_send_bytes: Some(peer.update_link_status_send_bytes),
                    update_link_status_ack_sends: Some(peer.update_link_status_ack_sends),
                    update_link_status_ack_send_bytes: Some(peer.update_link_status_ack_send_bytes),
                    round_trip_time_microseconds: Some(
                        stats.rtt.as_micros().try_into().unwrap_or(std::u64::MAX),
                    ),
                    congestion_window_bytes: Some(stats.cwnd as u64),
                }
            })
            .collect()
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
                    "{} [{}]: {}",
                    record.target(),
                    short_log_level(&record.level()),
                    record.args()
                );
            }
        }

        fn flush(&self) {}
    }

    static LOGGER: Logger = Logger;
    static START: Once = Once::new();

    pub fn init() {
        START.call_once(|| {
            log::set_logger(&LOGGER).unwrap();
            log::set_max_level(MAX_LOG_LEVEL);
        })
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

    const TEST_TIMEOUT_MS: u32 = 60_000;

    use super::test_util::*;
    use super::*;
    use timebomb::timeout_ms;

    #[derive(Debug)]
    enum IncomingMessage<'a> {
        ConnectService(u8, StreamId<u8>, &'a str, fidl_fuchsia_overnet::ConnectionInfo),
        BindChannel(u8, StreamId<u8>),
        ChannelRecv(u8, StreamId<u8>, &'a [u8]),
        UpdateNode(u8, NodeId, NodeDescription),
        Close(u8, StreamId<u8>),
    }

    #[test]
    fn no_op() {
        init();
        Router::<u8, Instant>::new();
        assert_eq!(
            Router::<u8, Instant>::new_with_options(RouterOptions::new().set_node_id(1.into()))
                .node_id
                .0,
            1
        );
    }

    struct TwoNode {
        router1: Router<u8, Instant>,
        router2: Router<u8, Instant>,
        link1: LinkId<u8>,
        link2: LinkId<u8>,
    }

    impl TwoNode {
        fn new() -> Self {
            let mut router1 = Router::new_with_options(test_router_options());
            let mut router2 = Router::new_with_options(test_router_options());
            let link1 = router1.new_link(router2.node_id, 123.into(), 1).unwrap();
            let link2 = router2.new_link(router1.node_id, 456.into(), 2).unwrap();
            router1.adjust_route(router2.node_id, link1).unwrap();
            router2.adjust_route(router1.node_id, link2).unwrap();

            TwoNode { router1, router2, link1, link2 }
        }

        fn step<OnIncoming>(&mut self, mut on_incoming: OnIncoming)
        where
            OnIncoming: FnMut(IncomingMessage<'_>) -> Result<(), Error>,
        {
            let router1 = &mut self.router1;
            let router2 = &mut self.router2;
            let link1 = self.link1;
            let link2 = self.link2;
            let now = Instant::now();
            router1.update_time(now);
            router2.update_time(now);
            struct R<'a, In>(u8, &'a mut In);
            impl<'a, In> MessageReceiver<u8> for R<'a, In>
            where
                In: FnMut(IncomingMessage<'_>) -> Result<(), Error>,
            {
                type Handle = ();
                fn connect_channel(
                    &mut self,
                    stream_id: StreamId<u8>,
                    service_name: &str,
                    connection_info: fidl_fuchsia_overnet::ConnectionInfo,
                ) -> Result<(), Error> {
                    (self.1)(IncomingMessage::ConnectService(
                        self.0,
                        stream_id,
                        service_name,
                        connection_info,
                    ))
                }
                fn bind_channel(&mut self, stream_id: StreamId<u8>) -> Result<(), Error> {
                    (self.1)(IncomingMessage::BindChannel(self.0, stream_id))
                }
                fn channel_recv(
                    &mut self,
                    stream_id: StreamId<u8>,
                    bytes: &mut Vec<u8>,
                    handles: &mut Vec<()>,
                ) -> Result<(), Error> {
                    assert!(handles.len() == 0);
                    (self.1)(IncomingMessage::ChannelRecv(self.0, stream_id, bytes.as_slice()))
                }
                fn update_node(&mut self, node_id: NodeId, desc: NodeDescription) {
                    (self.1)(IncomingMessage::UpdateNode(self.0, node_id, desc)).unwrap();
                }
                fn update_link(
                    &mut self,
                    _from: NodeId,
                    _to: NodeId,
                    _link_id: NodeLinkId,
                    _desc: LinkDescription,
                ) {
                }
                fn close(&mut self, stream_id: StreamId<u8>) {
                    (self.1)(IncomingMessage::Close(self.0, stream_id)).unwrap();
                }
                fn established_connection(&mut self, _node_id: NodeId) {}
            }
            router1.flush_recvs(&mut R(1, &mut on_incoming));
            router2.flush_recvs(&mut R(2, &mut on_incoming));
            router1.flush_sends(|link, data| {
                assert_eq!(*link, 1);
                router2.queue_recv(link2, data);
                Ok(())
            });
            router2.flush_sends(|link, data| {
                assert_eq!(*link, 2);
                router1.queue_recv(link1, data);
                Ok(())
            });
            router1.next_timeout();
            router2.next_timeout();
        }
    }

    #[test]
    fn connect() {
        init();
        timeout_ms(
            || {
                let mut env = TwoNode::new();
                while !env.router1.connected_to(env.router2.node_id) {
                    env.step(|frame| match frame {
                        IncomingMessage::UpdateNode(_, _, _) => Ok(()),
                        _ => unimplemented!(),
                    });
                }
            },
            TEST_TIMEOUT_MS,
        );
    }

    #[test]
    fn create_stream() {
        init();
        timeout_ms(
            || {
                let mut env = TwoNode::new();
                while !env.router1.connected_to(env.router2.node_id) {
                    env.step(|frame| match frame {
                        IncomingMessage::UpdateNode(_, _, _) => Ok(()),
                        _ => unimplemented!(),
                    });
                }
                let _stream1 = env.router1.new_stream(env.router2.node_id, "hello world").unwrap();
                let mut stream2: Option<StreamId<u8>> = None;
                let router1_node_id = env.router1.node_id;
                while stream2 == None {
                    env.step(|frame| {
                        Ok(match frame {
                            IncomingMessage::UpdateNode(_, _, _) => (),
                            IncomingMessage::ConnectService(
                                2,
                                id,
                                "hello world",
                                fidl_fuchsia_overnet::ConnectionInfo {
                                    peer:
                                        Some(fidl_fuchsia_overnet_protocol::NodeId { id: peer_id }),
                                },
                            ) => {
                                assert_eq!(router1_node_id, peer_id.into());
                                stream2 = Some(id);
                            }
                            x => {
                                panic!("{:?}", x);
                            }
                        })
                    });
                }
            },
            TEST_TIMEOUT_MS,
        );
    }

    #[test]
    fn send_datagram_immediately() {
        init();
        timeout_ms(
            || {
                let mut env = TwoNode::new();
                while !env.router1.connected_to(env.router2.node_id) {
                    env.step(|frame| match frame {
                        IncomingMessage::UpdateNode(_, _, _) => Ok(()),
                        _ => unimplemented!(),
                    });
                }
                let stream1 = env.router1.new_stream(env.router2.node_id, "hello world").unwrap();
                env.router1
                    .queue_send_channel_message(stream1, vec![1, 2, 3, 4, 5], vec![])
                    .unwrap();
                let mut stream2 = None;
                let mut got_packet = false;
                let router1_node_id = env.router1.node_id;
                while !got_packet {
                    env.step(|frame| {
                        Ok(match (stream2.is_none(), got_packet, frame) {
                            (_, _, IncomingMessage::UpdateNode(_, _, _)) => (),
                            (
                                true,
                                false,
                                IncomingMessage::ConnectService(
                                    2,
                                    id,
                                    "hello world",
                                    fidl_fuchsia_overnet::ConnectionInfo {
                                        peer: Some(fidl_fuchsia_overnet_protocol::NodeId { id: peer_id }),
                                    },
                                ),
                            ) => {
                                assert_eq!(router1_node_id, peer_id.into());
                                stream2 = Some(id);
                            }
                            (
                                false,
                                false,
                                IncomingMessage::ChannelRecv(2, id, &[1, 2, 3, 4, 5]),
                            ) => {
                                assert_eq!(id, stream2.unwrap());
                                got_packet = true;
                            }
                            x => {
                                panic!("{:?}", x);
                            }
                        })
                    });
                }
            },
            TEST_TIMEOUT_MS,
        );
    }

    #[test]
    fn ping_pong() {
        init();
        timeout_ms(
            || {
                let mut env = TwoNode::new();
                while !env.router1.connected_to(env.router2.node_id) {
                    env.step(|frame| match frame {
                        IncomingMessage::UpdateNode(_, _, _) => Ok(()),
                        _ => unimplemented!(),
                    });
                }
                let stream1 = env.router1.new_stream(env.router2.node_id, "hello world").unwrap();
                env.router1
                    .queue_send_channel_message(stream1, vec![1, 2, 3, 4, 5], vec![])
                    .unwrap();
                let mut stream2 = None;
                let mut got_ping = false;
                let mut sent_pong = false;
                let mut got_pong = false;
                let router1_node_id = env.router1.node_id;
                while !got_pong {
                    if got_ping && !sent_pong {
                        env.router2
                            .queue_send_channel_message(
                                stream2.unwrap(),
                                vec![9, 8, 7, 6, 5, 4, 3, 2, 1],
                                vec![],
                            )
                            .unwrap();
                        sent_pong = true;
                    }
                    env.step(|frame| {
                        Ok(match (stream2.is_none(), got_ping, sent_pong, got_pong, frame) {
                            (_, _, _, _, IncomingMessage::UpdateNode(_, _, _)) => (),
                            (
                                true,
                                false,
                                false,
                                false,
                                IncomingMessage::ConnectService(
                                    2,
                                    id,
                                    "hello world",
                                    fidl_fuchsia_overnet::ConnectionInfo {
                                        peer: Some(fidl_fuchsia_overnet_protocol::NodeId { id: peer_id }),
                                    },
                                ),
                            ) => {
                                assert_eq!(router1_node_id, peer_id.into());
                                stream2 = Some(id);
                            }
                            (
                                false,
                                false,
                                false,
                                false,
                                IncomingMessage::ChannelRecv(2, id, &[1, 2, 3, 4, 5]),
                            ) => {
                                assert_eq!(id, stream2.unwrap());
                                got_ping = true;
                            }
                            (
                                false,
                                true,
                                true,
                                false,
                                IncomingMessage::ChannelRecv(1, id, &[9, 8, 7, 6, 5, 4, 3, 2, 1]),
                            ) => {
                                assert_eq!(id, stream1);
                                got_pong = true;
                            }
                            _ => unreachable!(),
                        })
                    });
                }
            },
            TEST_TIMEOUT_MS,
        );
    }
}
