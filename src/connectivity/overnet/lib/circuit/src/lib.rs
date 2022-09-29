// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async::Timer;
use futures::channel::mpsc::{unbounded, UnboundedReceiver, UnboundedSender};
use futures::channel::oneshot;
use futures::future::Either;
use futures::lock::Mutex;
use futures::stream::StreamExt as _;
use futures::FutureExt;
use quic as _;
use quiche as _;
use std::collections::HashMap;
use std::future::Future;
use std::sync::{Arc, Weak};
use std::time::Duration;

pub const CIRCUIT_VERSION: u8 = 0;

mod error;
mod protocol;
mod stream;
#[cfg(test)]
mod test;

pub mod multi_stream;

use protocol::{EncodableString, Identify, NodeState};

pub use error::{Error, Result};
pub use protocol::Quality;

use crate::protocol::ProtocolMessage;

/// A list of other nodes we can see on the mesh. For each such node we retain a vector of senders
/// which allow us to establish new streams with that node. For each sender we also store the sum of
/// all quality values for hops to that peer (see `header::NodeState::Online`).
struct PeerMap {
    /// The actual map of peers itself.
    peers:
        HashMap<EncodableString, Vec<(UnboundedSender<(stream::Reader, stream::Writer)>, Quality)>>,
    /// This value increments once every time the peer map changes. Consequently, we can track
    /// changes in this number to determine when a routing refresh is necessary.
    generation: usize,
    /// This allows the router task to wait for this structure to update. Put simply, any time we
    /// increment `generation`, we should remove this sender from the struct and fire it if it is
    /// present.
    wakeup: Option<oneshot::Sender<()>>,
    /// Control channels for various peer nodes.
    ///
    /// Every time we directly connect to a new node, we start a stream called the control stream,
    /// which we use to send handshake messages and routing updates. Once we've written the
    /// handshake message, the write side of that control stream ends up here, and this is where we
    /// send routing updates.
    ///
    /// If we have a routing task, then any time the peer map updates, we will send updated node
    /// states to each channel contained in this `Vec`. The `HashMap` contains the state of the peer
    /// map the last time we sent info and is used to determine what updates need to be sent.
    ///
    /// If we don't have a routing task, this should be empty. In that case we're a "leaf node" and
    /// we don't send any routing information to peers at all.
    control_channels: Vec<(stream::Writer, HashMap<EncodableString, Quality>)>,
}

impl PeerMap {
    /// Create a new peer map.
    fn new() -> Self {
        PeerMap {
            peers: HashMap::new(),

            // Start with generation 1 so the routing task can start with generation 0. This means
            // the routing task is out of sync as soon as it starts, so it will immediately trigger
            // an update.
            generation: 1,

            wakeup: None,
            control_channels: Vec::new(),
        }
    }

    /// Increment the generation field and fire the wakeup sender if present.
    ///
    /// In short: this signals to the routing task that the peer map has been modified, so that it
    /// can update neighboring nodes with new routing information.
    fn increment_generation(&mut self) {
        self.generation += 1;
        self.wakeup.take().map(|x| {
            let _ = x.send(());
        });
    }

    /// Get the list of peers mutably, and signal the routing task that we have modified it.
    fn peers(
        &mut self,
    ) -> &mut HashMap<
        EncodableString,
        Vec<(UnboundedSender<(stream::Reader, stream::Writer)>, Quality)>,
    > {
        self.increment_generation();
        &mut self.peers
    }

    /// Reduces our peer map to a simpler form which contains only the name of each node we can see
    /// and the quality rating of the fastest connection we have to that node.
    fn condense_routes(&self) -> HashMap<EncodableString, Quality> {
        let mut ret = HashMap::new();

        for (key, value) in &self.peers {
            if value.is_empty() {
                continue;
            }

            ret.insert(key.clone(), value[0].1);
        }

        ret
    }

    /// Adds a control channel to `control_channels` and sends an initial route update for that
    /// channel.
    async fn add_control_channel(
        &mut self,
        channel: stream::Writer,
        quality: Quality,
    ) -> Result<()> {
        let routes = self.condense_routes();

        for (node, &route_quality) in &routes {
            let quality = quality.combine(route_quality);
            let state = NodeState::Online(node.clone(), quality);

            channel.write_protocol_message(&state)?;
        }

        self.control_channels.push((channel, routes));

        Ok(())
    }
}

/// This is all the state necessary to run the background task that keeps a connection to another
/// node running.
struct ConnectionState {
    /// ID of the local node.
    node_id: EncodableString,
    /// Quality of the connection. If we get a `NodeState::Online` for another node via this
    /// connection, the quality mentioned in that message is to the peer node. To account for the
    /// quality of the connection itself, we combine this quality with it.
    quality: Quality,
    /// If we want to open a new stream via this connection, we send a reader and writer through
    /// this channel. `ConnectionState` doesn't use this directly, but passes it through the peer
    /// map to other tasks that may need it.
    new_stream_sender: UnboundedSender<(stream::Reader, stream::Writer)>,
    /// A message from this channel indicates the other end of this connection wants to open a new
    /// stream through us. We receive a reader and writer for the new stream, and a oneshot sender
    /// through which we can send an error code if something goes wrong while establishing the
    /// stream.
    new_stream_receiver:
        UnboundedReceiver<(stream::Reader, stream::Writer, oneshot::Sender<Result<()>>)>,
    /// If we receive a stream from `new_stream_receiver` that is trying to connect to this node (as
    /// opposed to being forwarded to another node), we can send the reader and writer back out
    /// through this channel. The included `String` is the identity of the node on the other end of
    /// the stream.
    incoming_stream_sender: UnboundedSender<(stream::Reader, stream::Writer, String)>,
    /// Reader for the initial control stream that was established with this connection. We should
    /// see a handshake message come out of this, followed by routing updates. We'll always get at
    /// least one routing update where the node at the other end of the connection identifies
    /// itself. Further messages will be about nodes that we can forward to through that node. If
    /// the node we are directly connected to is a leaf node, there will thus be no further messages.
    control_reader: stream::Reader,
    /// Peer map for this node.
    peers: Arc<Mutex<PeerMap>>,
    /// Protocol identifier string sent with every handshake.
    protocol: EncodableString,

    /// Sender on which to announce new peers as they become available.
    new_peer_sender: UnboundedSender<String>,
}

impl ConnectionState {
    /// Runs all necessary background processing for a connection. Tasks include:
    /// 1) Send a handshake message to the other node.
    /// 2) Wait for and validate the handshake from the peer.
    /// 3) Receive updated routing information from the peer and update the peer map accordingly.
    /// 4) When the peer tries to open a new stream to us, either forward that stream to another
    ///    node or, if it's destined for us directly, consume the initial handshake from it and send
    ///    it out to be processed by the user.
    async fn run(mut self) -> Result<()> {
        // Start by reading and validating a handshake message from the stream.
        let header = self.control_reader.read_protocol_message::<Identify>().await?;

        if header.circuit_version != CIRCUIT_VERSION {
            return Err(Error::VersionMismatch);
        } else if header.protocol != self.protocol {
            return Err(Error::ProtocolMismatch);
        }

        // Reads routing updates from the control stream one at a time and updates the peer map.
        let read_control = {
            let peers = Arc::clone(&self.peers);
            let new_stream_sender = self.new_stream_sender.clone();
            let node_id = self.node_id.clone();
            async move {
                loop {
                    let state = match self.control_reader.read_protocol_message::<NodeState>().await
                    {
                        Err(Error::ConnectionClosed) => break Ok(()),
                        other => other?,
                    };

                    match state {
                        NodeState::Online(peer, quality) => {
                            if peer == node_id {
                                continue;
                            }

                            let quality = quality.combine(self.quality);
                            let mut peers = peers.lock().await;
                            let peers = peers.peers();
                            let peer_string = peer.to_string();
                            let peer_list = peers.entry(peer).or_insert_with(Vec::new);
                            if peer_list.is_empty() {
                                let _ = self.new_peer_sender.unbounded_send(peer_string);
                            }
                            peer_list.retain(|x| !x.0.same_receiver(&new_stream_sender));
                            peer_list.push((new_stream_sender.clone(), quality));
                            peer_list.sort_by_key(|x| x.1);
                        }
                        NodeState::Offline(peer) => {
                            let mut peers = peers.lock().await;
                            let peers = peers.peers();
                            let peer_list = peers.get_mut(&peer);

                            if let Some(peer_list) = peer_list {
                                peer_list.retain(|x| !x.0.same_receiver(&new_stream_sender));
                            }
                        }
                    }
                }
            }
        };

        // Responds from requests from the other end of the connection to start a new stream.
        let new_streams = {
            let peers = Arc::clone(&self.peers);
            async move {
                while let Some((reader, writer, result_sender)) =
                    self.new_stream_receiver.next().await
                {
                    let _ = result_sender.send(
                        async {
                            let dest = reader
                                .read(EncodableString::MIN_SIZE, |buf| {
                                    EncodableString::try_from_bytes(buf).map(|(dest, size)| {
                                        if dest == self.node_id {
                                            // If the destination is this node, discard the string
                                            // itself (we know where we are, so we don't need it)
                                            // and return as usual.
                                            (None, size)
                                        } else {
                                            // If the destination node is another node, return a
                                            // size of zero, which tells the reader to leave the
                                            // destination string in the stream. When we forward the
                                            // stream, the node we forward it to will be able to
                                            // read the destination again.
                                            (Some(dest), 0)
                                        }
                                    })
                                })
                                .await?;

                            if let Some(dest) = dest {
                                connect_to_peer(Arc::clone(&peers), reader, writer, &dest).await?;
                            } else {
                                let src = reader.read_protocol_message::<EncodableString>().await?;
                                self.incoming_stream_sender
                                    .unbounded_send((reader, writer, src.to_string()))
                                    .map_err(|_| Error::ConnectionClosed)?;
                            }
                            Ok(())
                        }
                        .await,
                    );
                }
            }
        };

        futures::pin_mut!(read_control);
        futures::pin_mut!(new_streams);

        let ret = match futures::future::select(read_control, new_streams).await {
            Either::Left((result, new_streams)) => {
                if result.is_ok() {
                    new_streams.await;
                }
                result
            }
            Either::Right(((), read_control)) => read_control.now_or_never().unwrap_or(Ok(())),
        };

        {
            let mut peers = self.peers.lock().await;
            let peers = peers.peers();
            for peer_list in peers.values_mut() {
                peer_list.retain(|x| !x.0.same_receiver(&self.new_stream_sender));
            }
        }

        ret
    }
}

/// A connection from one node to another.
pub struct Connection {
    /// See `take_control_stream`
    control_stream: Option<(stream::Reader, stream::Writer)>,
    /// State for the background task that keeps the connection functioning.
    state: Option<ConnectionState>,
    /// See `take_new_streams`,
    new_streams: Option<UnboundedReceiver<(stream::Reader, stream::Writer)>>,
    /// When the local node wants to create a new stream that travels via this connection, it can
    /// send the reader and writer for the service end of the stream here, along with a oneshot to
    /// receive an error code.
    new_stream_sender:
        UnboundedSender<(stream::Reader, stream::Writer, oneshot::Sender<Result<()>>)>,
}

impl Connection {
    /// When we connect to another node, we immediately start a "control stream" that performs a
    /// handshake and sends routing messages. This returns the "service side" of the control stream.
    /// When our connection writes to the control stream, the write will come out of the reader
    /// given here. It's up to whatever connection back end we have, e.g. the QUIC back end, to then
    /// forward that message over the actual underlying protocol.
    ///
    /// This method transfers ownership of the streams out of the `Connection` object, so it will
    /// return `Some` the first time it is called and `None` ever after.
    pub fn take_control_stream(&mut self) -> Option<(stream::Reader, stream::Writer)> {
        self.control_stream.take()
    }

    /// Called the first time, produces a future which must be polled to completion to keep the
    /// connection functioning. The future may complete early or may run for the entire lifetime of
    /// the connection.
    pub fn take_run_future(
        &mut self,
    ) -> Option<impl std::future::Future<Output = Result<()>> + Send> {
        self.state.take().map(ConnectionState::run)
    }

    /// Removes and returns a receiver which gives us the "service end" of new streams this
    /// connection wants to start.
    ///
    /// e.g. if the QUIC back end were servicing this connection, it would wait for values from the
    /// receiver returned here, then for each of them it would allocate a new stream on the
    /// underlying QUIC connection, and forward traffic through that newly-allocated stream from the
    /// reader and writer it obtained here.
    pub fn take_new_streams(
        &mut self,
    ) -> Option<UnboundedReceiver<(stream::Reader, stream::Writer)>> {
        self.new_streams.take()
    }

    /// Indicates to the connection that the back end wants to create a new stream.
    ///
    /// e.g. if the QUIC back end saw a new incoming stream request from the peer it was connected
    /// to, it would call this method, and forward the reader and writer it obtained therefrom to
    /// the new QUIC stream.
    pub async fn create_stream(
        &self,
        remote_reader: stream::Reader,
        remote_writer: stream::Writer,
    ) -> Result<()> {
        let (err_sender, err_receiver) = oneshot::channel();
        self.new_stream_sender
            .unbounded_send((remote_reader, remote_writer, err_sender))
            .map_err(|_| Error::ConnectionClosed)?;
        err_receiver.await.map_err(|_| Error::ConnectionClosed)??;
        Ok(())
    }
}

/// Represents a node on the circuit network.
///
/// A node can connect to one or more other nodes, and a stream can be established between any two
/// nodes on the entire connected graph of nodes.
pub struct Node {
    /// A unique identifying string for this node.
    node_id: EncodableString,
    /// Indicates what protocol we intend to run atop the streams we established, e.g.
    /// "Overnet-with-FIDL-ABI-1"
    protocol: EncodableString,
    /// List of other nodes we can "see" on the network.
    peers: Arc<Mutex<PeerMap>>,
    /// If true, we will inform nodes we are connected to of what other nodes we can see, so that
    /// they might try to forward traffic through us. This also indicates that there is a "router
    /// process" running for this node which handles said forwarding.
    has_router: bool,

    /// If another node establishes a connection to this node, we will notify the user by way of
    /// this sender.
    incoming_stream_sender: UnboundedSender<(stream::Reader, stream::Writer, String)>,

    /// We will hand this receiver to the user to notify them of incoming stream connections from
    /// other nodes.
    incoming_stream_receiver: Option<UnboundedReceiver<(stream::Reader, stream::Writer, String)>>,

    /// If a new peer becomes available we will send its name through this sender to notify the user.
    new_peer_sender: UnboundedSender<String>,

    /// We will hand this receiver to the user to notify them when new peers become available.
    new_peer_receiver: Option<UnboundedReceiver<String>>,
}

impl Node {
    /// Establish a new node.
    pub fn new(node_id: &str, protocol: &str) -> Result<Node> {
        let node_id = node_id.to_owned().try_into()?;
        let protocol = protocol.to_owned().try_into()?;

        let (incoming_stream_sender, incoming_stream_receiver) = unbounded();
        let (new_peer_sender, new_peer_receiver) = unbounded();
        let incoming_stream_receiver = Some(incoming_stream_receiver);
        let new_peer_receiver = Some(new_peer_receiver);
        Ok(Node {
            node_id,
            protocol,
            new_peer_sender,
            new_peer_receiver,
            incoming_stream_sender,
            incoming_stream_receiver,
            peers: Arc::new(Mutex::new(PeerMap::new())),
            has_router: false,
        })
    }

    /// Establish a new node which will forward streams between its peers. The router process
    /// provided must be polled continuously to provide this forwarding.
    pub fn new_with_router(
        node_id: &str,
        protocol: &str,
        interval: Duration,
    ) -> Result<(Node, impl Future<Output = ()> + Send)> {
        let mut node = Self::new(node_id, protocol)?;
        node.has_router = true;

        let weak_peers = Arc::downgrade(&node.peers);

        Ok((node, router(weak_peers, interval)))
    }

    /// Take possession of the incoming streams from this node. The returned `UnboundedReceiver`
    /// will provide a reader, writer, and source node ID every time another node establishes a
    /// stream with this one.
    pub fn take_incoming_streams(
        &mut self,
    ) -> Option<UnboundedReceiver<(stream::Reader, stream::Writer, String)>> {
        self.incoming_stream_receiver.take()
    }

    /// Take possession of the new peer stream from this connection. The returned
    /// `UnboundedReceiver` will provide a node ID for each new peer that becomes visible to this
    /// node.
    pub fn take_new_peers(&mut self) -> Option<UnboundedReceiver<String>> {
        self.new_peer_receiver.take()
    }

    /// Establish a stream with another node.
    pub async fn connect_to_peer(&self, node_id: &str) -> Result<(stream::Reader, stream::Writer)> {
        let (connection_reader, return_writer) = stream::stream();
        let (return_reader, connection_writer) = stream::stream();
        let connection_reader = connection_reader.into();

        if self.node_id == node_id {
            self.incoming_stream_sender
                .unbounded_send((connection_reader, connection_writer, node_id.to_owned()))
                .map_err(|_| Error::NoSuchPeer(node_id.to_owned()))?;
        } else {
            let node_id: EncodableString = node_id.to_owned().try_into()?;
            let source_node_id = self.node_id.clone();
            return_writer.write_protocol_message(&node_id)?;
            return_writer.write_protocol_message(&source_node_id)?;
            connect_to_peer(
                Arc::clone(&self.peers),
                connection_reader,
                connection_writer,
                &node_id,
            )
            .await?;
        }

        Ok((return_reader, return_writer))
    }

    /// Connect to another node.
    ///
    /// This establishes the internal state to connect this node directly to another one. To
    /// actually perform the networking necessary to create such a connection, a back end will have
    /// to service the returned `Connection` object.
    pub async fn connect_node(&self, quality: Quality) -> Connection {
        let (new_stream_sender, new_streams) = unbounded();
        let (initial_reader, control_writer) = stream::stream();
        let (control_reader, initial_writer) = stream::stream();
        let (new_remote_stream_sender, new_stream_receiver) = unbounded();

        let header = Identify::new(self.protocol.clone());
        control_writer.write_protocol_message(&header).expect("We just created this writer!");

        let state = NodeState::Online(self.node_id.clone(), Quality::SELF);
        control_writer.write_protocol_message(&state).expect("We just created this writer!");

        if self.has_router {
            self.peers
                .lock()
                .await
                .add_control_channel(control_writer, quality)
                .await
                .expect("We just created this channel!");
        }

        Connection {
            control_stream: Some((initial_reader, initial_writer)),
            new_streams: Some(new_streams),
            new_stream_sender: new_remote_stream_sender,
            state: Some(ConnectionState {
                node_id: self.node_id.clone(),
                quality,
                new_peer_sender: self.new_peer_sender.clone(),
                new_stream_sender,
                new_stream_receiver,
                incoming_stream_sender: self.incoming_stream_sender.clone(),
                control_reader,
                peers: Arc::clone(&self.peers),
                protocol: self.protocol.clone(),
            }),
        }
    }
}

/// Given the reader and writer for an incoming connection, forward that connection to another node.
async fn connect_to_peer(
    peers: Arc<Mutex<PeerMap>>,
    peer_reader: stream::Reader,
    peer_writer: stream::Writer,
    node_id: &EncodableString,
) -> Result<()> {
    let mut peers_lock = peers.lock().await;
    let peers = &mut peers_lock.peers;

    // For each peer we have a list of channels to which we can send our reader and writer, each
    // representing a connection which will become the next link in the circuit. The list is sorted
    // by connection quality, getting worse toward the end of the list, so we want to send our
    // reader and writer to the first one we can.
    let peer_list = peers.get_mut(node_id).ok_or_else(|| Error::NoSuchPeer(node_id.to_string()))?;

    let mut peer_channels = Some((peer_reader, peer_writer));
    let mut changed = false;

    // Go through each potential connection and send to the first one which will handle the
    // connection. We may discover the first few we try have hung up and gone away, so we'll delete
    // those from the list and try the next one.
    peer_list.retain_mut(|x| {
        if let Some((peer_reader, peer_writer)) = peer_channels.take() {
            match x.0.unbounded_send((peer_reader, peer_writer)) {
                Ok(()) => true,
                Err(e) => {
                    changed = true;
                    peer_channels = Some(e.into_inner());
                    false
                }
            }
        } else {
            true
        }
    });

    // If this is true, we cleared out some stale connections from the routing table. Send a routing
    // update to update our neighbors about how this might affect connectivity.
    if changed {
        peers_lock.increment_generation();
    }

    // Our iteration above should have taken channels and sent them along to the connection that
    // will handle them. If they're still here we didn't find a channel.
    if peer_channels.is_none() {
        Ok(())
    } else {
        Err(Error::NoSuchPeer(node_id.to_string()))
    }
}

/// Given an old and a new condensed routing table, create a serialized list of `NodeState`s which
/// will update a node on what has changed between them.
fn route_updates(
    old_routes: &HashMap<EncodableString, Quality>,
    new_routes: &HashMap<EncodableString, Quality>,
) -> Vec<u8> {
    let mut ret = Vec::new();

    for (node, &quality) in new_routes {
        if let Some(&old_quality) = old_routes.get(node) {
            if old_quality == quality {
                continue;
            }
        }

        NodeState::Online(node.clone(), quality).write_bytes_vec(&mut ret);
    }

    for old_node in old_routes.keys() {
        if !new_routes.contains_key(old_node) {
            NodeState::Offline(old_node.clone()).write_bytes_vec(&mut ret);
        }
    }

    ret
}

/// Router process. Notifies each peer every time the router table is updated. The given `interval`
/// allows these updates to be rate-limited; there will always be at least `interval` time between
/// updates.
async fn router(peers: Weak<Mutex<PeerMap>>, interval: Duration) {
    let mut generation = 0;

    loop {
        let mut wake_receiver = None;
        {
            let peers = if let Some(peers) = peers.upgrade() {
                peers
            } else {
                return;
            };
            let mut peers = peers.lock().await;

            if peers.generation <= generation {
                let (sender, receiver) = oneshot::channel();
                peers.wakeup = Some(sender);
                wake_receiver = Some(receiver);
            } else {
                let new_routes = peers.condense_routes();

                peers.control_channels.retain_mut(|(sender, routes)| {
                    let msgs = route_updates(routes, &new_routes);

                    if sender
                        .write(msgs.len(), |buf| {
                            buf[..msgs.len()].copy_from_slice(&msgs);
                            Ok(msgs.len())
                        })
                        .is_ok()
                    {
                        *routes = new_routes.clone();
                        true
                    } else {
                        false
                    }
                });

                generation = peers.generation;
            }
        }

        if let Some(receiver) = wake_receiver {
            let _ = receiver.await;
        } else {
            Timer::new(interval).await
        }
    }
}
