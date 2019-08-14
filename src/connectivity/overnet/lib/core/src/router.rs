// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Manages peer<->peer connections and routing packets over links between nodes.

// General structure:
// A router is a collection of: - streams (each with a StreamId)
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

use crate::coding::decode_fidl;
use crate::labels::{NodeId, NodeLinkId, RoutingLabel, VersionCounter, MAX_ROUTING_LABEL_LENGTH};
use crate::node_table::{LinkDescription, NodeDescription};
use failure::{Error, ResultExt};
use fidl_fuchsia_overnet_protocol::{
    ChannelHandle, ConnectToService, ConnectToServiceOptions, LinkStatus, PeerDescription,
    PeerMessage, ZirconChannelMessage, ZirconHandle,
};
use rand::Rng;
use salt_slab::{SaltSlab, SaltedID};
use std::collections::{btree_map, BTreeMap, BinaryHeap};
use std::time::Instant;

/// Designates one peer in the router
pub type PeerId = SaltedID;
/// Designates one link in the router
pub type LinkId = SaltedID;
/// Designates one stream in the router
pub type StreamId = SaltedID;

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

/// Describes a peer to this router.
/// For each peer we manage a quic connection and a set of streams over that quic connection.
/// For each node in the mesh we keep two peers: one client oriented, one server oriented (using
/// quic's notion of client/server).
/// ConnectToService requests travel from the client -> server, and all subsequent streams are
/// created on that connection.
struct Peer {
    /// The quic connection between ourselves and this peer.
    conn: Box<quiche::Connection>,
    /// The stream id of the connection stream for this peer.
    connection_stream_id: StreamId,
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
    streams: BTreeMap<u64, StreamId>,
    /// If known, a link that will likely be able to forward packets to this peers node_id.
    current_link: Option<LinkId>,
}

/// Was a given stream index initiated by this end of a quic connection?
fn is_local_quic_stream_index(stream_index: u64, is_client: bool) -> bool {
    (stream_index & 1) == (!is_client as u64)
}

/// Describes a stream from this node to some peer.
struct Stream {
    /// The peer that terminates this stream.
    peer_id: PeerId,
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
    Peer,
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

        trace!("recv bytes: state={:?} bytes={:?}", self, bytes);

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
                            warn!("Error reading stream: {:?}", e);
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
    quic_server_key_file: Option<String>,
    quic_server_cert_file: Option<String>,
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
    pub fn set_quic_server_key_file(mut self, key_file: &str) -> Self {
        self.quic_server_key_file = Some(key_file.to_string());
        self
    }

    /// Set which file to load the server cert from.
    pub fn set_quic_server_cert_file(mut self, pem_file: &str) -> Self {
        self.quic_server_cert_file = Some(pem_file.to_string());
        self
    }
}

/// During flush_recvs, defines the set of callbacks that may be made to notify the containing
/// application of updates received.
pub trait MessageReceiver {
    /// The type of handle this receiver deals with. Usually this is fidl::Handle, but for
    /// some tests it's convenient to vary this.
    type Handle;

    /// A connection request for some channel has been made.
    /// The new stream is `stream_id`, and the service requested is `service_name`.
    fn connect_channel<'a>(
        &mut self,
        stream_id: StreamId,
        service_name: &'a str,
    ) -> Result<(), Error>;
    /// A new channel stream is created at `stream_id`.
    fn bind_channel(&mut self, stream_id: StreamId) -> Result<Self::Handle, Error>;
    /// A channel stream `stream_id` received a datagram with `bytes` and `handles`.
    fn channel_recv(
        &mut self,
        stream_id: StreamId,
        bytes: &mut Vec<u8>,
        handles: &mut Vec<Self::Handle>,
    ) -> Result<(), Error>;
    /// A node `node_id` has updated its description to `desc`.
    fn update_node(&mut self, node_id: NodeId, desc: NodeDescription);
    /// Gossip about a link from `from` to `to` with id `link_id` has been received.
    /// This gossip is about version `version`, and updates the links description to
    /// `desc`.
    fn update_link(
        &mut self,
        from: NodeId,
        to: NodeId,
        link_id: NodeLinkId,
        version: VersionCounter,
        desc: LinkDescription,
    );
    /// A stream `stream_id` has been closed.
    fn close(&mut self, stream_id: StreamId);
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
struct Link<LinkData> {
    /// The node on the other end of this link.
    peer: NodeId,
    /// The client oriented peer associated with said node.
    client_peer: PeerId,
    /// The server oriented peer associated with said node (if it exists).
    server_peer: Option<PeerId>,
    /// The application data required to lookup this link.
    link_data: LinkData,
}

/// Records a timeout request at some time for a peer.
/// The last tuple element is a generation counter so we can detect expired timeouts.
#[derive(Clone, Copy, PartialEq, Eq, Ord)]
struct PeerTimeout<Time: RouterTime>(Time, PeerId, u64);

/// Sort time ascending.
impl<Time: RouterTime> PartialOrd for PeerTimeout<Time> {
    fn partial_cmp(&self, rhs: &Self) -> Option<std::cmp::Ordering> {
        Some(self.0.cmp(&rhs.0).reverse().then(self.1.cmp(&rhs.1)))
    }
}

/// The part of Router that deals only with streams and peers (but not links).
/// Separated to make satisfying the borrow checker a little more tractable without having to
/// write monster functions.
struct Endpoints<Time: RouterTime> {
    /// Node id of this router.
    node_id: NodeId,
    /// All peers.
    peers: SaltSlab<Peer>,
    /// All streams.
    streams: SaltSlab<Stream>,
    /// Mapping of node id -> client oriented peer id.
    node_to_client_peer: BTreeMap<NodeId, PeerId>,
    /// Mapping of node id -> server oriented peer id.
    node_to_server_peer: BTreeMap<NodeId, PeerId>,
    /// Peers waiting to be written (has pending_writes==true).
    write_peers: BinaryHeap<PeerId>,
    /// Peers waiting to be read (has pending_reads==true).
    read_peers: BinaryHeap<PeerId>,
    /// Peers that need their timeouts checked (having check_timeout==true).
    check_timeouts: BinaryHeap<PeerId>,
    /// Client oriented peer connections that have become established.
    newly_established_clients: BinaryHeap<PeerId>,
    /// Timeouts to get to at some point.
    queued_timeouts: BinaryHeap<PeerTimeout<Time>>,
    /// The last timestamp we saw.
    now: Time,
    /// The current description of our node id, formatted into a serialized description packet
    /// so we can quickly send it at need.
    node_desc_packet: Vec<u8>,
    /// The servers private key file for this node.
    server_key_file: Option<String>,
    /// The servers private cert file for this node.
    server_cert_file: Option<String>,
}

impl<Time: RouterTime> Endpoints<Time> {
    /// Update the routing table for `dest` to use `link_id`.
    fn adjust_route(&mut self, dest: NodeId, link_id: LinkId) -> Result<(), Error> {
        let server_peer_id = self.server_peer_id(dest);
        if let Some(peer_id) = server_peer_id {
            let peer = self.peers.get_mut(peer_id).unwrap();
            peer.current_link = Some(link_id);
        }
        let peer_id = self.client_peer_id(dest)?;
        let peer = self.peers.get_mut(peer_id).unwrap();
        peer.current_link = Some(link_id);
        Ok(())
    }

    /// Return true if there's a client oriented established connection to `dest`.
    fn connected_to(&self, dest: NodeId) -> bool {
        if let Some(peer_id) = self.node_to_client_peer.get(&dest) {
            let peer = self.peers.get(*peer_id).unwrap();
            peer.conn.is_established()
        } else {
            false
        }
    }

    /// Create a new stream to advertised service `service` on remote node id `node`.
    fn new_stream(&mut self, node: NodeId, service: &str) -> Result<StreamId, Error> {
        assert_ne!(node, self.node_id);
        let peer_id = self.client_peer_id(node)?;
        let peer = self.peers.get_mut(peer_id).unwrap();
        let id = peer.next_stream.checked_shl(2).ok_or_else(|| format_err!("Too many streams"))?;
        peer.next_stream += 1;
        let connection_stream_id = peer.connection_stream_id;
        let stream_id = self.streams.insert(Stream {
            peer_id,
            id,
            read_state: ReadState::new_bound(StreamType::Channel),
        });
        peer.streams.insert(id, stream_id);
        fidl::encoding::with_tls_encoded(
            &mut PeerMessage::ConnectToService(ConnectToService {
                service_name: service.to_string(),
                stream_id: id,
                options: ConnectToServiceOptions {},
            }),
            |mut bytes: &mut Vec<u8>, handles: &mut Vec<fidl::Handle>| {
                if handles.len() > 0 {
                    bail!("Expected no handles");
                }
                self.queue_send_raw_datagram(connection_stream_id, Some(&mut bytes), false)
            },
        )?;
        Ok(stream_id)
    }

    /// Regenerate our description packet and send it to all peers.
    fn publish_node_description(&mut self, services: Vec<String>) -> Result<(), Error> {
        self.node_desc_packet = make_desc_packet(services)?;
        let stream_ids: Vec<StreamId> = self
            .peers
            .iter_mut()
            .filter(|peer| peer.is_client && peer.conn.is_established())
            .map(|peer| peer.connection_stream_id)
            .collect();
        for stream_id in stream_ids {
            let mut desc = self.node_desc_packet.clone();
            if let Err(e) = self.queue_send_raw_datagram(stream_id, Some(&mut desc), false) {
                warn!("Failed to send datagram: {:?}", e);
            }
        }
        Ok(())
    }

    /// Send a datagram on a channel type stream.
    fn queue_send_channel_message(
        &mut self,
        stream_id: StreamId,
        bytes: Vec<u8>,
        handles: Vec<SendHandle>,
    ) -> Result<Vec<StreamId>, Error> {
        let stream = self
            .streams
            .get(stream_id)
            .ok_or_else(|| format_err!("Stream not found {:?}", stream_id))?;
        let peer_id = stream.peer_id;
        let peer = self
            .peers
            .get_mut(peer_id)
            .ok_or_else(|| format_err!("Peer not found {:?}", peer_id))?;
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
                (send, stream_id)
            })
            .unzip();
        fidl::encoding::with_tls_encoded(
            &mut ZirconChannelMessage { bytes, handles },
            |bytes, handles| {
                if handles.len() != 0 {
                    bail!("Unexpected handles in encoding");
                }
                self.queue_send_raw_datagram(stream_id, Some(bytes), false)
            },
        )?;
        Ok(stream_ids)
    }

    /// Send a datagram on a stream.
    fn queue_send_raw_datagram(
        &mut self,
        stream_id: StreamId,
        frame: Option<&mut [u8]>,
        fin: bool,
    ) -> Result<(), Error> {
        trace!(
            "{:?} queue_send: stream_id={:?} frame={:?} fin={:?}",
            self.node_id,
            stream_id,
            frame,
            fin
        );
        let stream = self
            .streams
            .get(stream_id)
            .ok_or_else(|| format_err!("Stream not found {:?}", stream_id))?;
        trace!("  peer_id={:?} stream_index={}", stream.peer_id, stream.id);
        let peer_id = stream.peer_id;
        let peer = self
            .peers
            .get_mut(peer_id)
            .ok_or_else(|| format_err!("Peer not found {:?}", peer_id))?;
        if let Some(frame) = frame {
            let frame_len = frame.len();
            assert!(frame_len <= 0xffff_ffff);
            let header: [u8; 4] = [
                (frame_len & 0xff) as u8,
                ((frame_len >> 8) & 0xff) as u8,
                ((frame_len >> 16) & 0xff) as u8,
                ((frame_len >> 24) & 0xff) as u8,
            ];
            peer.conn.stream_send(stream.id, &header, false)?;
            peer.conn.stream_send(stream.id, frame, fin)?;
        } else {
            peer.conn.stream_send(stream.id, &mut [], fin)?;
        }
        if !peer.pending_writes {
            trace!("Mark writable: {:?}", stream.peer_id);
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
        let cert_file =
            self.server_cert_file.as_ref().ok_or_else(|| format_err!("No cert file for server"))?;
        let key_file =
            self.server_key_file.as_ref().ok_or_else(|| format_err!("No key file for server"))?;
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
    fn queue_recv<LinkData>(
        &mut self,
        link_id: LinkId,
        link: &mut Link<LinkData>,
        packet: &mut [u8],
    ) -> Result<(), Error> {
        let (routing_label, packet_length) = RoutingLabel::decode(link.peer, self.node_id, packet)?;
        println!(
            "routing_label={:?} packet_length={} src_len={}",
            routing_label,
            packet_length,
            packet.len()
        );
        let packet = &mut packet[..packet_length];

        let (peer_id, forward) = match (routing_label.to_client, routing_label.dst == self.node_id)
        {
            (false, true) => {
                let hdr = quiche::Header::from_slice(packet, quiche::MAX_CONN_ID_LEN)
                    .context("Decoding quic header")?;

                if hdr.ty == quiche::Type::VersionNegotiation {
                    bail!("Version negotiation invalid on the server");
                }

                if let Some(server_peer_id) = link.server_peer {
                    (server_peer_id, false)
                } else if hdr.ty == quiche::Type::Initial {
                    let mut config = self.server_config()?;
                    let scid: Vec<u8> = rand::thread_rng()
                        .sample_iter(&rand::distributions::Standard)
                        .take(quiche::MAX_CONN_ID_LEN)
                        .collect();
                    let conn = quiche::accept(&scid, None, &mut config)
                        .context("Creating quic server connection")?;
                    let connection_stream_id = self.streams.insert(Stream {
                        peer_id: PeerId::invalid(),
                        id: 0,
                        read_state: ReadState::new_bound(StreamType::Peer),
                    });
                    let mut streams = BTreeMap::new();
                    streams.insert(0, connection_stream_id);
                    let peer_id = self.peers.insert(Peer {
                        conn,
                        next_stream: 1,
                        node_id: link.peer,
                        current_link: None,
                        is_client: false,
                        pending_reads: false,
                        pending_writes: true,
                        check_timeout: false,
                        establishing: true,
                        timeout_generation: 0,
                        forward: Vec::new(),
                        streams,
                        connection_stream_id,
                    });
                    self.streams.get_mut(connection_stream_id).unwrap().peer_id = peer_id;
                    link.server_peer = Some(peer_id);
                    self.node_to_server_peer.insert(link.peer, peer_id);
                    self.write_peers.push(peer_id);
                    (peer_id, false)
                } else {
                    bail!("No server for link {:?}, and not an Initial packet", link_id);
                }
            }
            (true, true) => (link.client_peer, false),
            (false, false) => {
                let peer_id = self.server_peer_id(routing_label.dst).ok_or_else(|| {
                    format_err!("Node server peer not found for {:?}", routing_label.dst)
                })?;
                (peer_id, true)
            }
            (true, false) => {
                let peer_id = self.client_peer_id(routing_label.dst)?;
                (peer_id, true)
            }
        };

        // Guaranteed correct ID by peer_index above.
        let peer = self.peers.get_mut(peer_id).unwrap();
        if peer.current_link.is_none() {
            peer.current_link = Some(link_id);
        }
        if forward {
            peer.forward.push((routing_label, packet.to_vec()));
        } else {
            peer.conn.recv(packet).context("Receiving packet on quic connection")?;
            if !peer.pending_reads {
                trace!("{:?} Mark readable: {:?}", self.node_id, peer_id);
                peer.pending_reads = true;
                self.read_peers.push(peer_id);
            }
            if peer.establishing && peer.conn.is_established() {
                peer.establishing = false;
                if peer.is_client {
                    self.newly_established_clients.push(peer.connection_stream_id);
                }
            }
        }
        if !peer.pending_writes {
            trace!("{:?} Mark writable: {:?}", self.node_id, peer_id);
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
        Got: MessageReceiver,
    {
        let mut buf = [0_u8; MAX_RECV_LEN];

        while let Some(peer_id) = self.read_peers.pop() {
            if let Err(e) = self.recv_from_peer_id(peer_id, &mut buf, got) {
                warn!("Error receiving packet: {:?}", e);
                unimplemented!();
            }
        }
    }

    /// Helper for flush_recvs to process incoming packets from one peer.
    fn recv_from_peer_id<Got>(
        &mut self,
        peer_id: PeerId,
        buf: &mut [u8],
        got: &mut Got,
    ) -> Result<(), Error>
    where
        Got: MessageReceiver,
    {
        trace!("{:?} flush_recvs {:?}", self.node_id, peer_id);
        let mut peer = self
            .peers
            .get_mut(peer_id)
            .ok_or_else(|| format_err!("Peer not found {:?}", peer_id))?;
        assert!(peer.pending_reads);
        peer.pending_reads = false;
        // TODO(ctiller): We currently copy out the readable streams here just to satisfy the
        // bounds checker. Find a way to avoid this wasteful allocation.
        let readable: Vec<u64> = peer.conn.readable().collect();
        for stream_index in readable {
            trace!("{:?}   stream {} is readable", self.node_id, stream_index);
            let mut stream_id = peer.streams.get(&stream_index).copied();
            if stream_id.is_none() && !is_local_quic_stream_index(stream_index, peer.is_client) {
                stream_id = Some(self.streams.insert(Stream {
                    peer_id,
                    id: stream_index,
                    read_state: ReadState::new_unbound(),
                }));
                peer.streams.insert(stream_index, stream_id.unwrap());
            }
            if stream_id.is_none() {
                trace!("Stream {} not found", stream_index);
                unimplemented!();
            }
            let mut rs = self
                .streams
                .get_mut(stream_id.unwrap())
                .ok_or_else(|| format_err!("Stream not found {:?}", stream_id))?
                .read_state
                .take();
            let remove = loop {
                match peer.conn.stream_recv(stream_index, buf) {
                    Ok((n, fin)) => {
                        let mut recv_message_context = RecvMessageContext {
                            got,
                            peer_id,
                            peer: &mut peer,
                            stream_id: stream_id.unwrap(),
                            stream_index,
                            streams: &mut self.streams,
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
                .ok_or_else(|| format_err!("Stream not found {:?}", stream_id))?
                .read_state = rs;
            if remove {
                unimplemented!();
            }
        }
        Ok(())
    }

    /// Map a node id to the client peer id. Since we can always create a client peer, this will
    /// attempt to initiate a connection if there is no current connection.
    fn client_peer_id(&mut self, node_id: NodeId) -> Result<PeerId, Error> {
        if let Some(peer_id) = self.node_to_client_peer.get(&node_id) {
            return Ok(*peer_id);
        }

        let mut config = self.client_config()?;
        let scid: Vec<u8> = rand::thread_rng()
            .sample_iter(&rand::distributions::Standard)
            .take(quiche::MAX_CONN_ID_LEN)
            .collect();
        let conn =
            quiche::connect(None, &scid, &mut config).context("creating quic client connection")?;
        let connection_stream_id = self.streams.insert(Stream {
            peer_id: PeerId::invalid(),
            id: 0,
            read_state: ReadState::new_bound(StreamType::Peer),
        });
        let mut streams = BTreeMap::new();
        streams.insert(0, connection_stream_id);
        let peer_id = self.peers.insert(Peer {
            conn,
            next_stream: 1,
            node_id,
            current_link: None,
            is_client: true,
            pending_reads: false,
            pending_writes: true,
            check_timeout: false,
            establishing: true,
            timeout_generation: 0,
            forward: Vec::new(),
            streams,
            connection_stream_id,
        });
        self.streams.get_mut(connection_stream_id).unwrap().peer_id = peer_id;
        self.node_to_client_peer.insert(node_id, peer_id);
        self.write_peers.push(peer_id);
        Ok(peer_id)
    }

    /// Retrieve an existing server peer id for some node id.
    fn server_peer_id(&mut self, node_id: NodeId) -> Option<PeerId> {
        self.node_to_server_peer.get(&node_id).copied()
    }

    /// Update timers.
    fn update_time(&mut self, tm: Time) {
        self.now = tm;
        while let Some(PeerTimeout(when, peer_id, timeout_generation)) = self.queued_timeouts.peek()
        {
            if tm < *when {
                break;
            }
            if let Some(peer) = self.peers.get_mut(*peer_id) {
                if peer.timeout_generation != *timeout_generation {
                    self.queued_timeouts.pop();
                    continue;
                }
                trace!(
                    "{:?} expire timeout {:?} gen={}",
                    self.node_id,
                    peer_id,
                    peer.timeout_generation
                );
                peer.conn.on_timeout();
                if !peer.check_timeout {
                    peer.check_timeout = true;
                    self.check_timeouts.push(*peer_id);
                }
                if !peer.pending_writes {
                    peer.pending_writes = true;
                    self.write_peers.push(*peer_id);
                }
                if peer.establishing && peer.conn.is_established() {
                    peer.establishing = false;
                    if peer.is_client {
                        self.newly_established_clients.push(peer.connection_stream_id);
                    }
                }
            }
            self.queued_timeouts.pop();
        }
    }

    /// Calculate when the next timeout should be triggered.
    fn next_timeout(&mut self) -> Option<Time> {
        while let Some(peer_id) = self.check_timeouts.pop() {
            if let Some(peer) = self.peers.get_mut(peer_id) {
                assert!(peer.check_timeout);
                peer.check_timeout = false;
                peer.timeout_generation += 1;
                if let Some(duration) = peer.conn.timeout() {
                    let when = Time::after(self.now, duration.into());
                    trace!(
                        "{:?} queue timeout in {:?} in {:?} [gen={}]",
                        self.node_id,
                        peer_id,
                        duration,
                        peer.timeout_generation
                    );
                    self.queued_timeouts.push(PeerTimeout(when, peer_id, peer.timeout_generation));
                }
            }
        }
        self.queued_timeouts.peek().map(|PeerTimeout(when, _, _)| *when)
    }

    /// Write out any sends that have been queued by us (avoids some borrow checker problems).
    fn flush_internal_sends(&mut self) {
        while let Some(stream_id) = self.newly_established_clients.pop() {
            if let Err(e) = self.queue_send_raw_datagram(
                stream_id,
                Some(&mut self.node_desc_packet.clone()),
                false,
            ) {
                warn!("Failed to send initial update to {:?}: {:?}", stream_id, e);
            }
        }
    }
}

/// Maximum length of a received packet from quic.
const MAX_RECV_LEN: usize = 1500;

/// Helper to receive one message.
/// Borrows of all relevant data structures from Endpoints (but only them).
struct RecvMessageContext<'a, Got> {
    got: &'a mut Got,
    peer_id: PeerId,
    peer: &'a mut Peer,
    stream_index: u64,
    stream_id: StreamId,
    streams: &'a mut SaltSlab<Stream>,
}

impl<'a, Got> RecvMessageContext<'a, Got>
where
    Got: MessageReceiver,
{
    /// Receive a datagram, parse it, and dispatch it to the right methods.
    /// message==None => end of stream.
    fn recv(&mut self, stream_type: StreamType, message: Option<&mut [u8]>) -> Result<(), Error> {
        trace!(
            "Peer {:?} stream {:?} index {} gets {:?}",
            self.peer_id,
            self.stream_id,
            self.stream_index,
            message
        );

        match (stream_type, message) {
            (StreamType::Peer, None) => unimplemented!(),
            (_, None) => {
                self.got.close(self.stream_id);
                Ok(())
            }
            (StreamType::Channel, Some(bytes)) => {
                let mut msg = ZirconChannelMessage { bytes: Vec::new(), handles: Vec::new() };
                fidl::encoding::Decoder::decode_into(bytes, &mut [], &mut msg)?;
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
                        _ => bail!("Bad handle type"),
                    };
                    handles.push(bound);
                }
                self.got.channel_recv(self.stream_id, &mut msg.bytes, &mut handles)
            }
            (StreamType::Peer, Some(bytes)) => {
                match decode_fidl::<PeerMessage>(bytes).context("Decoding PeerMessage")? {
                    PeerMessage::ConnectToService(ConnectToService {
                        service_name: service,
                        stream_id: new_stream_index,
                        options: _,
                    }) => {
                        trace!(
                            "Log new connection request: peer {:?} new_stream_index {:?} service: {}",
                            self.peer_id,
                            new_stream_index,
                            service
                        );
                        self.bind_stream(StreamType::Channel, new_stream_index, |stream_id, got| {
                            got.connect_channel(stream_id, &service)
                        })
                    }
                    PeerMessage::UpdateNodeDescription(PeerDescription { services }) => {
                        self.got.update_node(
                            self.peer.node_id,
                            NodeDescription { services: services.unwrap_or(vec![]) },
                        );
                        Ok(())
                    }
                    PeerMessage::UpdateLinkStatus(LinkStatus {
                        from,
                        to,
                        local_id,
                        version,
                        metrics,
                    }) => {
                        self.got.update_link(
                            from.id.into(),
                            to.id.into(),
                            local_id.into(),
                            version.into(),
                            LinkDescription {
                                round_trip_time: std::time::Duration::from_micros(
                                    metrics.rtt.unwrap_or(std::u64::MAX),
                                ),
                            },
                        );
                        Ok(())
                    }
                    x => {
                        bail!("Unknown variant: {:?}", x);
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
        mut app_bind: impl FnMut(StreamId, &mut Got) -> Result<R, Error>,
    ) -> Result<R, Error> {
        if stream_index == 0 {
            bail!("Cannot connect stream 0");
        }
        let bind_stream_id = match self.peer.streams.entry(stream_index) {
            btree_map::Entry::Occupied(stream_id) => *stream_id.get(),
            btree_map::Entry::Vacant(v) => {
                let stream_id = *v.insert(self.streams.insert(Stream {
                    peer_id: self.peer_id,
                    id: stream_index,
                    read_state: ReadState::new_bound(stream_type),
                }));
                trace!("Bind stream stream_index={} no current read_state", stream_index);
                // early return
                return app_bind(stream_id, self.got);
            }
        };
        let result = app_bind(bind_stream_id, self.got)?;
        let mut rs = self
            .streams
            .get_mut(bind_stream_id)
            .ok_or_else(|| format_err!("Stream not found for bind {:?}", bind_stream_id))?
            .read_state
            .take();
        trace!("Bind stream stream_index={} read_state={:?}", stream_index, rs);
        let mut recv_message_context = RecvMessageContext {
            got: self.got,
            peer_id: self.peer_id,
            peer: self.peer,
            stream_id: bind_stream_id,
            stream_index,
            streams: self.streams,
        };
        rs = rs.bind(stream_type, |stream_type: StreamType, message: Option<&mut [u8]>| {
            recv_message_context.recv(stream_type, message)
        });
        self.streams
            .get_mut(bind_stream_id)
            .ok_or_else(|| format_err!("Stream not found for bind {:?}", bind_stream_id))?
            .read_state = rs;
        Ok(result)
    }
}

/// Router maintains global state for one node_id.
pub struct Router<LinkData, Time: RouterTime> {
    node_id: NodeId,
    links: SaltSlab<Link<LinkData>>,
    endpoints: Endpoints<Time>,
}

fn make_desc_packet(services: Vec<String>) -> Result<Vec<u8>, Error> {
    fidl::encoding::with_tls_encoded(
        &mut PeerMessage::UpdateNodeDescription(PeerDescription { services: Some(services) }),
        |bytes, handles| {
            if handles.len() != 0 {
                bail!("Unexpected handles in encoding");
            }
            Ok(bytes.clone())
        },
    )
}

impl<LinkData, Time: RouterTime> Router<LinkData, Time> {
    /// New with some set of options
    pub fn new_with_options(options: RouterOptions) -> Self {
        let node_id = options.node_id.unwrap_or_else(|| rand::thread_rng().gen::<u64>().into());
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
                node_desc_packet: make_desc_packet(vec![]).unwrap(),
                now: Time::now(),
                server_cert_file: options.quic_server_cert_file,
                server_key_file: options.quic_server_key_file,
            },
            links: SaltSlab::new(),
        }
    }

    /// New object with default options
    pub fn new() -> Self {
        Self::new_with_options(RouterOptions::new())
    }

    /// Create a new link to some node, returning a `LinkId` describing it.
    pub fn new_link(&mut self, peer: NodeId, link_data: LinkData) -> Result<LinkId, Error> {
        let client_peer = self.endpoints.client_peer_id(peer)?;
        let server_peer = self.endpoints.server_peer_id(peer);
        let link_id = self.links.insert(Link { peer, client_peer, server_peer, link_data });
        if let Some(peer) = self.endpoints.peers.get_mut(client_peer) {
            if peer.current_link.is_none() {
                peer.current_link = Some(link_id);
            }
        }
        if let Some(server_peer) = server_peer {
            if let Some(peer) = self.endpoints.peers.get_mut(server_peer) {
                if peer.current_link.is_none() {
                    peer.current_link = Some(link_id);
                }
            }
        }
        Ok(link_id)
    }

    /// Accessor for the node id of this router.
    pub fn node_id(&self) -> NodeId {
        self.node_id
    }

    /// Drop a link that is no longer needed.
    pub fn drop_link(&mut self, link_id: LinkId) {
        self.links.remove(link_id);
    }

    /// Create a new stream to advertised service `service` on remote node id `node`.
    pub fn new_stream(&mut self, node: NodeId, service: &str) -> Result<StreamId, Error> {
        self.endpoints.new_stream(node, service)
    }

    /// Update the routing table for `dest` to use `link_id`.
    pub fn adjust_route(&mut self, dest: NodeId, link_id: LinkId) -> Result<(), Error> {
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
        stream_id: StreamId,
        bytes: Vec<u8>,
        handles: Vec<SendHandle>,
    ) -> Result<Vec<StreamId>, Error> {
        self.endpoints.queue_send_channel_message(stream_id, bytes, handles)
    }

    /// Send a datagram on a stream.
    pub fn queue_send_raw_datagram(
        &mut self,
        stream_id: StreamId,
        frame: Option<&mut [u8]>,
        fin: bool,
    ) -> Result<(), Error> {
        self.endpoints.queue_send_raw_datagram(stream_id, frame, fin)
    }

    /// Receive a packet from some link.
    pub fn queue_recv(&mut self, link_id: LinkId, packet: &mut [u8]) -> Result<(), Error> {
        if packet.len() < 1 {
            return Ok(());
        }

        if let Some(link) = self.links.get_mut(link_id) {
            self.endpoints.queue_recv(link_id, link, packet)
        } else {
            bail!("No link {:?}", link_id)
        }
    }

    /// Flush outstanding packets destined to one peer to links, via `send_to`
    fn flush_sends_to_peer<SendTo>(
        &mut self,
        send_to: &mut SendTo,
        peer_id: PeerId,
        buf: &mut [u8],
    ) -> Result<(), Error>
    where
        SendTo: FnMut(&LinkData, &mut [u8]) -> Result<(), Error>,
    {
        let buf_len = buf.len();
        trace!("{:?} flush_sends {:?}", self.node_id, peer_id);
        let peer = self
            .endpoints
            .peers
            .get_mut(peer_id)
            .ok_or_else(|| format_err!("Peer {:?} not found for sending", peer_id))?;
        assert!(peer.pending_writes);
        peer.pending_writes = false;
        let current_link = peer
            .current_link
            .ok_or_else(|| format_err!("No current link for peer {:?}", peer_id))?;
        let link = self.links.get(current_link).ok_or_else(|| {
            format_err!("Current link {:?} for peer {:?} not found", current_link, peer_id)
        })?;
        let peer_node_id = peer.node_id;
        let link_dst_id = link.peer;
        let is_peer_client = peer.is_client;
        let src_node_id = self.node_id;
        for forward in peer.forward.drain(..) {
            let mut packet = forward.1;
            forward.0.encode_for_link(src_node_id, link_dst_id, &mut packet)?;
            if let Err(e) = send_to(&link.link_data, packet.as_mut_slice()) {
                warn!("Error sending: {:?}", e);
            }
        }
        loop {
            match peer.conn.send(&mut buf[..buf_len - MAX_ROUTING_LABEL_LENGTH]) {
                Ok(n) => {
                    let suffix_len = RoutingLabel {
                        src: src_node_id,
                        dst: peer_node_id,
                        to_client: !is_peer_client,
                    }
                    .encode_for_link(
                        src_node_id,
                        link_dst_id,
                        &mut buf[n..],
                    )?;
                    if let Err(e) = send_to(&link.link_data, &mut buf[..n + suffix_len]) {
                        warn!("Error sending: {:?}", e);
                    }
                    if !peer.check_timeout {
                        peer.check_timeout = true;
                        self.endpoints.check_timeouts.push(peer_id);
                    }
                    if peer.establishing && peer.conn.is_established() {
                        peer.establishing = false;
                        if peer.is_client {
                            self.endpoints
                                .newly_established_clients
                                .push(peer.connection_stream_id);
                        }
                    }
                }
                Err(quiche::Error::Done) => {
                    if peer.establishing && peer.conn.is_established() {
                        peer.establishing = false;
                        if peer.is_client {
                            self.endpoints
                                .newly_established_clients
                                .push(peer.connection_stream_id);
                        }
                    }
                    trace!("{:?} sends done {:?}", self.node_id, peer_id);
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
        self.endpoints.flush_internal_sends();

        const MAX_LEN: usize = 1500;
        let mut buf = [0_u8; MAX_LEN];

        while let Some(peer_id) = self.endpoints.write_peers.pop() {
            if let Err(e) = self.flush_sends_to_peer(&mut send_to, peer_id, &mut buf) {
                warn!("Send to peer failed: {:?}", e);
            }
        }
    }

    /// Process all received packets and make callbacks depending on what was contained in them.
    pub fn flush_recvs<Got>(&mut self, got: &mut Got)
    where
        Got: MessageReceiver,
    {
        self.endpoints.flush_recvs(got);
    }

    /// Update timers.
    pub fn update_time(&mut self, tm: Time) {
        self.endpoints.update_time(tm)
    }

    /// Calculate when the next timeout should be triggered.
    pub fn next_timeout(&mut self) -> Option<Time> {
        self.endpoints.next_timeout()
    }
}

#[cfg(test)]
mod tests {

    use std::sync::Once;

    const TEST_TIMEOUT_MS: u32 = 60_000;

    const LOG_LEVEL: log::Level = log::Level::Trace;
    const MAX_LOG_LEVEL: log::LevelFilter = log::LevelFilter::Trace;

    struct Logger;

    #[derive(Debug)]
    enum IncomingMessage<'a> {
        ConnectService(u8, StreamId, &'a str),
        BindChannel(u8, StreamId),
        ChannelRecv(u8, StreamId, &'a [u8]),
        UpdateNode(u8, NodeId, NodeDescription),
        Close(u8, StreamId),
    }

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
        fn enabled(&self, metadata: &log::Metadata) -> bool {
            metadata.level() <= LOG_LEVEL
        }

        fn log(&self, record: &log::Record) {
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

    fn init() {
        START.call_once(|| {
            log::set_logger(&LOGGER).unwrap();
            log::set_max_level(MAX_LOG_LEVEL);
        })
    }

    use super::*;
    use timebomb::timeout_ms;

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
        link1: LinkId,
        link2: LinkId,
    }

    impl TwoNode {
        fn options() -> RouterOptions {
            let options = RouterOptions::new();
            #[cfg(target_os = "fuchsia")]
            let options = options
                .set_quic_server_cert_file("/pkg/data/cert.crt")
                .set_quic_server_key_file("/pkg/data/cert.key");
            options
        }

        fn new() -> Self {
            let mut router1 = Router::new_with_options(TwoNode::options());
            let mut router2 = Router::new_with_options(TwoNode::options());
            let link1 = router1.new_link(router2.node_id, 1).unwrap();
            let link2 = router2.new_link(router1.node_id, 2).unwrap();
            router1.adjust_route(router2.node_id, link1).unwrap();
            router2.adjust_route(router1.node_id, link2).unwrap();

            TwoNode { router1, router2, link1, link2 }
        }

        fn step<OnIncoming>(&mut self, mut on_incoming: OnIncoming)
        where
            OnIncoming: FnMut(IncomingMessage) -> Result<(), Error>,
        {
            let router1 = &mut self.router1;
            let router2 = &mut self.router2;
            let link1 = self.link1;
            let link2 = self.link2;
            let now = Instant::now();
            router1.update_time(now);
            router2.update_time(now);
            struct R<'a, In>(u8, &'a mut In);
            impl<'a, In> MessageReceiver for R<'a, In>
            where
                In: FnMut(IncomingMessage) -> Result<(), Error>,
            {
                type Handle = ();
                fn connect_channel(
                    &mut self,
                    stream_id: StreamId,
                    service_name: &str,
                ) -> Result<(), Error> {
                    (self.1)(IncomingMessage::ConnectService(self.0, stream_id, service_name))
                }
                fn bind_channel(&mut self, stream_id: StreamId) -> Result<(), Error> {
                    (self.1)(IncomingMessage::BindChannel(self.0, stream_id))
                }
                fn channel_recv(
                    &mut self,
                    stream_id: StreamId,
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
                    _version: VersionCounter,
                    _desc: LinkDescription,
                ) {
                    unimplemented!();
                }
                fn close(&mut self, stream_id: StreamId) {
                    (self.1)(IncomingMessage::Close(self.0, stream_id)).unwrap();
                }
            }
            router1.flush_recvs(&mut R(1, &mut on_incoming));
            router2.flush_recvs(&mut R(2, &mut on_incoming));
            router1.flush_sends(|link, data| {
                assert_eq!(*link, 1);
                router2.queue_recv(link2, data).unwrap();
                Ok(())
            });
            router2.flush_sends(|link, data| {
                assert_eq!(*link, 2);
                router1.queue_recv(link1, data).unwrap();
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
                let mut stream2: Option<StreamId> = None;
                while stream2 == None {
                    env.step(|frame| {
                        Ok(match frame {
                            IncomingMessage::UpdateNode(_, _, _) => (),
                            IncomingMessage::ConnectService(2, id, "hello world") => {
                                stream2 = Some(id)
                            }
                            _ => unreachable!(),
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
                while !got_packet {
                    env.step(|frame| {
                        Ok(match (stream2.is_none(), got_packet, frame) {
                            (_, _, IncomingMessage::UpdateNode(_, _, _)) => (),
                            (
                                true,
                                false,
                                IncomingMessage::ConnectService(2, id, "hello world"),
                            ) => stream2 = Some(id),
                            (
                                false,
                                false,
                                IncomingMessage::ChannelRecv(2, id, &[1, 2, 3, 4, 5]),
                            ) => {
                                assert_eq!(id, stream2.unwrap());
                                got_packet = true;
                            }
                            x => {
                                trace!("{:?}", x);
                                unreachable!()
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
                                IncomingMessage::ConnectService(2, id, "hello world"),
                            ) => stream2 = Some(id),
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
