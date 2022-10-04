// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::stream;
use crate::{Error, Node, Result};

use futures::channel::mpsc::{unbounded, UnboundedReceiver, UnboundedSender};
use futures::channel::oneshot;
use futures::lock::Mutex;
use futures::stream::Stream;
use futures::StreamExt;
use rand::random;
use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::io::Write;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Weak};
use std::time::Duration;

// We're stuffing enough u64s into streams that this is worth doing.
impl crate::protocol::ProtocolMessage for u64 {
    const MIN_SIZE: usize = 8;

    fn write_bytes<W: Write>(&self, out: &mut W) -> Result<usize> {
        out.write_all(&self.to_le_bytes())?;
        Ok(8)
    }

    fn byte_size(&self) -> usize {
        8
    }

    fn try_from_bytes(bytes: &[u8]) -> Result<(Self, usize)> {
        Ok((u64::from_le_bytes(bytes.try_into().map_err(|_| Error::BufferTooShort(8))?), 8))
    }
}

/// Entry in a stream map. See `StreamMap` below.
enum StreamMapEntry {
    /// The user is expecting the other end of the connection to start a stream with a certain ID,
    /// but we haven't actually seen the stream show up yet.
    Waiting(oneshot::Sender<(stream::Reader, stream::Writer)>),
    /// The other end of the connection has started a stream with a given ID, but we're still
    /// waiting on the user on this end to accept it by invoking `bind_stream()`.
    Ready(stream::Reader, stream::Writer),
    /// A stream with a given ID was started by the other end of the connection, and accepted by
    /// this end. If we see another stream with that ID something has gone wrong in the protocol
    /// state machine.
    Taken,
}

#[derive(Copy, Clone, Debug)]
enum ClientOrServer {
    Client,
    Server,
}

impl ClientOrServer {
    fn is_server(&self) -> bool {
        matches!(self, Self::Server)
    }
}

impl StreamMapEntry {
    /// Turns the `Ready` state into the `Taken` state and returns the reader and writer that were
    /// consumed. Returns `None` if we were not in the `Ready` state.
    fn take(&mut self) -> Option<(stream::Reader, stream::Writer)> {
        match std::mem::replace(self, Self::Taken) {
            Self::Waiting(_) | Self::Taken => None,
            Self::Ready(r, w) => Some((r, w)),
        }
    }

    /// Turns the `Waiting` state into the `Taken` state, passing the given reader and writer to the
    /// waiter. If we're not in the `Waiting` state this drops the reader and writer and does
    /// nothing.
    fn ready(&mut self, reader: stream::Reader, writer: stream::Writer) {
        if let Self::Waiting(sender) = std::mem::replace(self, Self::Taken) {
            if let Err((reader, writer)) = sender.send((reader, writer)) {
                *self = Self::Ready(reader, writer)
            }
        }
    }
}

/// A Mutex-protected map of stream IDs to an optional reader/writer pair for the stream.
type StreamMap = Mutex<HashMap<u64, StreamMapEntry>>;

/// A connection is a group of streams that span from one node to another on the circuit network.
///
/// When we establish a network of circuit nodes, we can create a stream from any one to any other.
/// These streams are independent, however; they have no name or identifier, nor anything else by
/// which you might group them.
///
/// A `Connection` is a link from a node to a peer that we can obtain streams from, just as we can
/// from the `Node` itself, but the streams have IDs which are in a namespace unique to the
/// connection. Multiple connections can exist per node, and each sees only the streams related to
/// itself.
///
/// There's a small bit of added protocol associated with this, so there will be some change to
/// traffic on the wire.
#[derive(Clone)]
pub struct Connection {
    id: u64,
    streams: Arc<StreamMap>,
    node: Arc<Node>,
    peer_node_id: String,
    next_stream_id: Arc<AtomicU64>,
}

impl Connection {
    pub async fn bind_stream(&self, id: u64) -> Option<(stream::Reader, stream::Writer)> {
        let receiver = {
            match self.streams.lock().await.entry(id) {
                Entry::Occupied(mut e) => return e.get_mut().take(),
                Entry::Vacant(v) => {
                    let (sender, receiver) = oneshot::channel();
                    v.insert(StreamMapEntry::Waiting(sender));
                    receiver
                }
            }
        };

        receiver.await.ok()
    }

    pub fn from(&self) -> &str {
        &self.peer_node_id
    }

    /// Create a new stream to the other end of this connection.
    pub async fn alloc_stream(
        &self,
        reader: stream::Reader,
        writer: stream::Writer,
    ) -> Result<u64> {
        let id = self.next_stream_id.fetch_add(2, Ordering::Relaxed);
        reader.push_back_protocol_message(&id)?;
        reader.push_back_protocol_message(&self.id)?;
        self.node.connect_to_peer(reader, writer, &self.peer_node_id).await?;
        Ok(id)
    }

    /// Whether this connection is a client (initiated by another node) as opposed to a server
    /// (initiated by this node).
    pub fn is_client(&self) -> bool {
        is_client_stream_id(self.next_stream_id.load(Ordering::Relaxed))
    }
}

impl std::fmt::Debug for Connection {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Connection({:#x} to {})", self.id, self.peer_node_id)
    }
}

/// Wrapper class for a `Node` that lets us create `Connection` objects instead of creating raw
/// streams on the node itself.
pub struct ConnectionNode {
    node: Arc<Node>,
    conns: Arc<Mutex<HashMap<u64, (Weak<StreamMap>, ClientOrServer)>>>,
}

/// Streams initiated by the client (the end of the connection that initiated the connection) should
/// be even, while streams initiated by the server (the end of the connection that received the
/// connection) should be odd.
fn is_client_stream_id(id: u64) -> bool {
    (id & 1) == 0
}

impl ConnectionNode {
    /// Create a new `ConnectionNode`. We can create a `Connection` object for any peer via this
    /// node, and we can then create streams to that peer and have that peer create streams to us.
    /// Unlike with a raw `Node`, those streams will be associated with only our connection object,
    /// and the peer will get a connection object that will return to it only the streams we create
    /// from this connection object.
    ///
    /// Returns both a `ConnectionNode`, and a `futures::stream::Stream` of `Connection` objects,
    /// which are produced by other nodes connecting to us.
    pub fn new(
        node_id: &str,
        protocol: &str,
        new_peer_sender: UnboundedSender<String>,
    ) -> Result<(ConnectionNode, impl Stream<Item = Connection> + Send)> {
        let (incoming_stream_sender, incoming_stream_receiver) = unbounded();
        let node = Arc::new(Node::new(node_id, protocol, new_peer_sender, incoming_stream_sender)?);
        let conns = Arc::new(Mutex::new(HashMap::<u64, (Weak<StreamMap>, ClientOrServer)>::new()));
        let conn_stream =
            conn_stream(Arc::downgrade(&node), Arc::clone(&conns), incoming_stream_receiver);

        Ok((ConnectionNode { node, conns }, conn_stream))
    }

    /// Like `ConnectionNode::new` but creates a router task as well. See `Node::new_with_router`.
    pub fn new_with_router(
        node_id: &str,
        protocol: &str,
        interval: Duration,
        new_peer_sender: UnboundedSender<String>,
    ) -> Result<(ConnectionNode, impl Stream<Item = Connection> + Send)> {
        let (incoming_stream_sender, incoming_stream_receiver) = unbounded();
        let (node, router) = Node::new_with_router(
            node_id,
            protocol,
            interval,
            new_peer_sender,
            incoming_stream_sender,
        )?;
        let node = Arc::new(node);
        let conns = Arc::new(Mutex::new(HashMap::<u64, (Weak<StreamMap>, ClientOrServer)>::new()));

        // With some cleverness, we can make polling the conn stream poll the router as a side effect.
        let conn_stream =
            conn_stream(Arc::downgrade(&node), Arc::clone(&conns), incoming_stream_receiver)
                .map(Some);
        let router_stream = futures::stream::once(router).map(|()| None);
        let conn_stream =
            futures::stream::select(conn_stream, router_stream).filter_map(|x| async move { x });

        Ok((ConnectionNode { node, conns }, conn_stream))
    }

    /// Establish a connection to a peer. The `connection_reader` and `connection_writer` will be
    /// used to service stream ID 0, which is always created when we start a connection.
    pub async fn connect_to_peer(
        &self,
        node_id: &str,
        connection_reader: stream::Reader,
        connection_writer: stream::Writer,
    ) -> Result<Connection> {
        let id = random();

        // Create stream 0 automatically. This will have the side effect of verifying connectivity
        // to the node as well.
        connection_reader.push_back_protocol_message(&0u64)?;
        connection_reader.push_back_protocol_message(&id)?;
        self.node.connect_to_peer(connection_reader, connection_writer, node_id).await?;
        let streams = Arc::new(Mutex::new(HashMap::new()));

        self.conns.lock().await.insert(id, (Arc::downgrade(&streams), ClientOrServer::Client));

        Ok(Connection {
            id,
            streams,
            node: Arc::clone(&self.node),
            peer_node_id: node_id.to_string(),
            next_stream_id: Arc::new(AtomicU64::new(2)),
        })
    }

    /// Get this node as a plain old `Node`.
    pub fn node(&self) -> &Node {
        &*self.node
    }
}

/// Creates a futures::Stream that when polled, will yield incoming streams on a particular
/// connection.
///
/// Polling is also responsible for dispatching incoming streams from the node to existing
/// connections.
fn conn_stream(
    node: Weak<Node>,
    conns: Arc<Mutex<HashMap<u64, (Weak<StreamMap>, ClientOrServer)>>>,
    incoming_stream_receiver: UnboundedReceiver<(stream::Reader, stream::Writer, String)>,
) -> impl Stream<Item = Connection> + Send {
    incoming_stream_receiver.filter_map(move |(reader, writer, peer_node_id)| {
        let conns = Arc::clone(&conns);
        let node = node.upgrade();
        async move {
            let node = node?;
            let got = reader
                .read(16, |buf| {
                    Ok((
                        (
                            u64::from_le_bytes(buf[..8].try_into().unwrap()),
                            u64::from_le_bytes(buf[8..16].try_into().unwrap()),
                        ),
                        16,
                    ))
                })
                .await;

            let (conn_id, stream_id) = match got {
                Ok(got) => got,
                Err(Error::ConnectionClosed) => return None,
                _ => unreachable!("Deserializing the connection ID should never fail!"),
            };

            let mut conns = conns.lock().await;

            if let Some((streams, client_or_server)) = conns.get(&conn_id) {
                if let Some(streams) = streams.upgrade() {
                    if is_client_stream_id(stream_id) == client_or_server.is_server() {
                        match streams.lock().await.entry(stream_id) {
                            Entry::Occupied(mut o) => o.get_mut().ready(reader, writer),
                            Entry::Vacant(v) => {
                                v.insert(StreamMapEntry::Ready(reader, writer));
                            }
                        }
                    } else {
                        tracing::warn!(stream_id, end = ?client_or_server.is_server(),
                        "Peer initiated stream ID which does not match role");
                    }
                }
                None
            } else if stream_id == 0 {
                let mut streams = HashMap::new();
                streams.insert(stream_id, StreamMapEntry::Ready(reader, writer));
                let streams = Arc::new(Mutex::new(streams));
                conns.insert(conn_id, (Arc::downgrade(&streams), ClientOrServer::Server));
                Some(Connection {
                    id: conn_id,
                    streams,
                    node,
                    peer_node_id,
                    next_stream_id: Arc::new(AtomicU64::new(1)),
                })
            } else {
                tracing::warn!(conn_id, stream_id, "Connection does not exist");
                None
            }
        }
    })
}
