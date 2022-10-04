// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async::Timer;
use futures::channel::mpsc::{UnboundedReceiver, UnboundedSender};
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
#[cfg(test)]
mod test;

mod connection;
pub mod multi_stream;
pub mod stream;

use protocol::{EncodableString, Identify, NodeState};

pub use connection::{Connection, ConnectionNode};
pub use error::{Error, Result};
pub use protocol::Quality;

use crate::protocol::ProtocolMessage;

/// A list of other nodes we can see on the mesh. For each such node we retain a vector of senders
/// which allow us to establish new streams with that node. Each sender in the vector corresponds to
/// a different path we could take to the desired peer. For each sender we also store the sum of all
/// quality values for hops to that peer (see `header::NodeState::Online`).
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

    /// If a new peer becomes available we will send its name through this sender to notify the user.
    new_peer_sender: UnboundedSender<String>,
}

impl Node {
    /// Establish a new node.
    ///
    /// Any time a new peer becomes visible to this node, the peer's node ID will be sent to
    /// `new_peer_sender`.
    ///
    /// Any time a peer wants to establish a stream with this node, a reader and writer for the new
    /// stream as well as the peer's node ID will be sent to `incoming_stream_sender`.
    pub fn new(
        node_id: &str,
        protocol: &str,
        new_peer_sender: UnboundedSender<String>,
        incoming_stream_sender: UnboundedSender<(stream::Reader, stream::Writer, String)>,
    ) -> Result<Node> {
        let node_id = node_id.to_owned().try_into()?;
        let protocol = protocol.to_owned().try_into()?;

        Ok(Node {
            node_id,
            protocol,
            new_peer_sender,
            incoming_stream_sender,
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
        new_peer_sender: UnboundedSender<String>,
        incoming_stream_sender: UnboundedSender<(stream::Reader, stream::Writer, String)>,
    ) -> Result<(Node, impl Future<Output = ()> + Send)> {
        let mut node = Self::new(node_id, protocol, new_peer_sender, incoming_stream_sender)?;
        node.has_router = true;

        let weak_peers = Arc::downgrade(&node.peers);

        Ok((node, router(weak_peers, interval)))
    }

    /// Establish a stream with another node. Data will be sent to the peer with
    /// `connection_writer`, and received with `connection_reader`.
    pub async fn connect_to_peer(
        &self,
        connection_reader: stream::Reader,
        connection_writer: stream::Writer,
        node_id: &str,
    ) -> Result<()> {
        if self.node_id == node_id {
            self.incoming_stream_sender
                .unbounded_send((connection_reader, connection_writer, node_id.to_owned()))
                .map_err(|_| Error::NoSuchPeer(node_id.to_owned()))?;
        } else {
            let node_id: EncodableString = node_id.to_owned().try_into()?;
            // Write the destination node ID and the source node ID (us). Keep in mind because of
            // the semantics of push_back_protocol_message, these will come off the wire in the
            // *reverse* of the order we're putting them on here.
            connection_reader.push_back_protocol_message(&self.node_id)?;
            connection_reader.push_back_protocol_message(&node_id)?;
            connect_to_peer(
                Arc::clone(&self.peers),
                connection_reader,
                connection_writer,
                &node_id,
            )
            .await?;
        }

        Ok(())
    }

    /// Connect to another node.
    ///
    /// This establishes the internal state to link this node directly to another one, there by
    /// joining it to the circuit network. To actually perform the networking necessary to create
    /// such a link, a back end will have to service the streams given to this function via
    /// its arguments. To keep the link running, the returned future must also be polled to
    /// completion. Depending on configuration, it may complete swiftly or may poll for the entire
    /// lifetime of the link.
    ///
    /// When we link to another node, we immediately start a "control stream" that performs a
    /// handshake and sends routing messages. The reader and writer passed in through
    /// `control_stream` will be used to service this stream. If `control_stream` is `None` the
    /// first stream emitted from `new_stream_receiver` will be used.
    ///
    /// When the local node needs to create a new stream to the linked node, it will send a reader
    /// and writer to `new_stream_sender`.
    ///
    /// When the linked node wants to create a new stream to this node, the back end may send a
    /// reader and writer through `new_stream_receiver`, as well as a `oneshot::Sender` which will
    /// be used to report if the link is established successfully or if an error occurs.
    ///
    /// The returned future will continue to poll for the lifetime of the link and return the error
    /// that terminated it.
    pub fn link_node(
        &self,
        control_stream: Option<(stream::Reader, stream::Writer)>,
        new_stream_sender: UnboundedSender<(stream::Reader, stream::Writer)>,
        mut new_stream_receiver: UnboundedReceiver<(
            stream::Reader,
            stream::Writer,
            oneshot::Sender<Result<()>>,
        )>,
        quality: Quality,
    ) -> impl Future<Output = Result<()>> + Send {
        let has_router = self.has_router;
        let peers = Arc::clone(&self.peers);
        let node_id = self.node_id.clone();
        let protocol = self.protocol.clone();
        let (new_stream_receiver_sender, new_stream_receiver_receiver) = oneshot::channel();
        let (control_reader_sender, control_reader_receiver) = oneshot::channel();
        let new_streams_loop = self.handle_new_streams(new_stream_receiver_receiver);
        let control_stream_loop =
            self.handle_control_stream(control_reader_receiver, new_stream_sender.clone(), quality);

        // Runs all necessary background processing for a connection. Tasks include:
        // 1) Fetch a control stream from the new_stream_receiver if we don't have one from
        //    control_stream already.
        // 2) Send a handshake message to the other node.
        // 3) Wait for and validate the handshake from the peer.
        // 4) Receive updated routing information from the peer and update the peer map accordingly.
        // 5) When the peer tries to open a new stream to us, either forward that stream to another
        //    node or, if it's destined for us directly, consume the initial handshake from it and send
        //    it out to be processed by the user.
        async move {
            let (control_reader, control_writer) = if let Some(control_stream) = control_stream {
                control_stream
            } else {
                let (reader, writer, error_sender) =
                    new_stream_receiver.next().await.ok_or(Error::ConnectionClosed)?;
                let _ = error_sender.send(Ok(()));
                (reader, writer)
            };

            let _ = new_stream_receiver_sender.send(new_stream_receiver);

            let header = Identify::new(protocol.clone());
            control_writer.write_protocol_message(&header)?;

            let state = NodeState::Online(node_id.clone(), Quality::SELF);
            control_writer.write_protocol_message(&state)?;

            if has_router {
                peers
                    .lock()
                    .await
                    .add_control_channel(control_writer, quality)
                    .await
                    .expect("We just created this channel!");
            } else {
                // No router means no further routing messages. Just let 'er go.
                std::mem::drop(control_writer);
            }

            // Start by reading and validating a handshake message from the stream.
            let header = control_reader.read_protocol_message::<Identify>().await?;

            if header.circuit_version != CIRCUIT_VERSION {
                return Err(Error::VersionMismatch);
            } else if header.protocol != protocol {
                return Err(Error::ProtocolMismatch);
            }

            control_reader_sender.send(control_reader).map_err(|_| Error::ConnectionClosed)?;

            futures::pin_mut!(control_stream_loop);
            futures::pin_mut!(new_streams_loop);

            let ret = match futures::future::select(control_stream_loop, new_streams_loop).await {
                Either::Left((result, new_streams)) => {
                    if result.is_ok() {
                        new_streams.await;
                    }
                    result
                }
                Either::Right(((), read_control)) => read_control.now_or_never().unwrap_or(Ok(())),
            };

            {
                let mut peers = peers.lock().await;
                let peers = peers.peers();
                for peer_list in peers.values_mut() {
                    peer_list.retain(|x| !x.0.same_receiver(&new_stream_sender));
                }
            }

            ret
        }
    }

    /// Handles messages we receive on a control stream, i.e. routing updates. `new_stream_sender`
    /// is a channel by which streams destined for another peer can be forwarded to the node on the
    /// other end of this control stream. The routing table will associate that sender with any
    /// peers that can be reached via that node.
    ///
    /// The returned future will poll until the control stream hangs up or a protocol error occurs.
    fn handle_control_stream(
        &self,
        control_reader: oneshot::Receiver<stream::Reader>,
        new_stream_sender: UnboundedSender<(stream::Reader, stream::Writer)>,
        quality: Quality,
    ) -> impl Future<Output = Result<()>> + Send {
        let peers = Arc::clone(&self.peers);
        let new_stream_sender = new_stream_sender.clone();
        let node_id = self.node_id.clone();
        let new_peer_sender = self.new_peer_sender.clone();

        async move {
            let control_reader = control_reader.await.map_err(|_| Error::ConnectionClosed)?;
            loop {
                let state = match control_reader.read_protocol_message::<NodeState>().await {
                    Err(Error::ConnectionClosed) => break Ok(()),
                    other => other?,
                };

                match state {
                    NodeState::Online(peer, path_quality) => {
                        if peer == node_id {
                            continue;
                        }

                        let quality = path_quality.combine(quality);
                        let mut peers = peers.lock().await;
                        let peers = peers.peers();
                        let peer_string = peer.to_string();
                        let peer_list = peers.entry(peer).or_insert_with(Vec::new);
                        if peer_list.is_empty() {
                            let _ = new_peer_sender.unbounded_send(peer_string);
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
    }

    /// Handles requests for new streams. The `new_stream_receiver` provides a reader and writer for
    /// every stream that gets established to or through this node, as well as a `Result` sender so
    /// we can indicate if we have any trouble handling this stream. We read a bit of protocol
    /// header from each incoming stream and either accept it or forward it to another peer.
    ///
    /// The returned future will poll until the back end hangs up the other end of the receiver.
    fn handle_new_streams(
        &self,
        new_stream_receiver_receiver: oneshot::Receiver<
            UnboundedReceiver<(stream::Reader, stream::Writer, oneshot::Sender<Result<()>>)>,
        >,
    ) -> impl Future<Output = ()> {
        let peers = Arc::clone(&self.peers);
        let incoming_stream_sender = self.incoming_stream_sender.clone();
        let node_id = self.node_id.clone();
        async move {
            let mut new_stream_receiver = if let Ok(x) = new_stream_receiver_receiver.await {
                x
            } else {
                return;
            };

            while let Some((reader, writer, result_sender)) = new_stream_receiver.next().await {
                let _ = result_sender.send(
                    async {
                        let dest = reader
                            .read(EncodableString::MIN_SIZE, |buf| {
                                EncodableString::try_from_bytes(buf).map(|(dest, size)| {
                                    if dest == node_id {
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
                            incoming_stream_sender
                                .unbounded_send((reader, writer, src.to_string()))
                                .map_err(|_| Error::ConnectionClosed)?;
                        }
                        Ok(())
                    }
                    .await,
                );
            }
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
