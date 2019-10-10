// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Represents one node in the mesh. Usually the root of a process.

use {
    crate::{
        labels::{NodeId, NodeLinkId},
        node_table::{LinkDescription, NodeDescription, NodeStateCallback, NodeTable},
        router::{
            LinkId, MessageReceiver, Router, RouterOptions, RouterTime, SendHandle, StreamId,
        },
        stream_framer::{StreamDeframer, StreamFramer},
    },
    failure::Error,
    fidl::{AsyncChannel, Channel, Handle, HandleBased},
    fidl_fuchsia_overnet::{Peer, ServiceProviderMarker, ServiceProviderProxy},
    fidl_fuchsia_overnet_protocol::{PeerDescription, StreamSocketGreeting},
    futures::prelude::*,
    rand::seq::SliceRandom,
    salt_slab::{SaltSlab, SaltedID, ShadowSlab},
    std::{
        cell::RefCell,
        collections::{BTreeMap, HashMap},
        fmt::Debug,
        rc::Rc,
    },
};

/// The runtime facilities needed to manage a node.
/// For portability these are injected from above.
pub trait NodeRuntime {
    /// Something representing an instant of time
    type Time: RouterTime + std::fmt::Debug;
    /// Application local identifier for a link
    type LinkId: Copy + Debug;

    /// Determine the type of a handle
    fn handle_type(hdl: &Handle) -> Result<SendHandle, Error>;
    /// Spawn a future on the current thread
    fn spawn_local<F>(&mut self, future: F)
    where
        F: Future<Output = ()> + 'static;
    ///  Execute `f` at time `t`
    fn at(&mut self, t: Self::Time, f: impl FnOnce() + 'static);
    /// Convert an application link id to a router link id
    fn router_link_id(&self, id: Self::LinkId) -> LinkId;
    /// Send `packet` on application link `id`
    fn send_on_link(&mut self, id: Self::LinkId, packet: &mut [u8]) -> Result<(), Error>;
}

/// Binding of a stream to some communications structure.
enum StreamBinding {
    Channel(Rc<AsyncChannel>),
}

/// Implementation of core::MessageReceiver for overnetstack.
/// Maintains a small list of borrows from the App instance that creates it.
struct Receiver<'a, Runtime: NodeRuntime> {
    service_map: &'a HashMap<String, ServiceProviderProxy>,
    streams: &'a mut ShadowSlab<StreamBinding>,
    node_table: &'a mut NodeTable,
    runtime: &'a mut Runtime,
    update_routing: bool,
    node: Node<Runtime>,
}

impl<'a, Runtime: NodeRuntime + 'static> Receiver<'a, Runtime> {
    /// Given a stream_id create a channel to pass to an application as the outward facing
    /// interface.
    fn make_channel(&mut self, stream_id: StreamId) -> Result<Channel, Error> {
        let (overnet_channel, app_channel) = Channel::create()?;
        let overnet_channel = Rc::new(AsyncChannel::from_channel(overnet_channel)?);
        self.streams.init(stream_id, StreamBinding::Channel(overnet_channel.clone()));
        self.runtime.spawn_local(self.node.clone().channel_reader(overnet_channel, stream_id));
        Ok(app_channel)
    }
}

impl<'a, Runtime: NodeRuntime + 'static> MessageReceiver for Receiver<'a, Runtime> {
    type Handle = Handle;

    fn connect_channel(&mut self, stream_id: StreamId, service_name: &str) -> Result<(), Error> {
        let app_channel = self.make_channel(stream_id)?;
        self.service_map
            .get(service_name)
            .ok_or_else(|| format_err!("Unknown service {}", service_name))?
            .connect_to_service(app_channel)?;
        Ok(())
    }

    fn bind_channel(&mut self, stream_id: StreamId) -> Result<Self::Handle, Error> {
        Ok(self.make_channel(stream_id)?.into_handle())
    }

    fn channel_recv(
        &mut self,
        stream_id: StreamId,
        bytes: &mut Vec<u8>,
        handles: &mut Vec<Self::Handle>,
    ) -> Result<(), Error> {
        let stream = self.streams.get(stream_id).ok_or_else(|| {
            format_err!("Stream {:?} not found for datagram {:?}", stream_id, bytes)
        })?;
        match stream {
            StreamBinding::Channel(ref chan) => {
                chan.write(bytes, handles)?;
                Ok(())
            }
        }
    }

    fn close(&mut self, stream_id: StreamId) {
        self.streams.remove(stream_id);
    }

    fn update_node(&mut self, node_id: NodeId, desc: NodeDescription) {
        self.node_table.update_node(node_id, desc);
    }

    fn update_link(&mut self, from: NodeId, to: NodeId, link: NodeLinkId, desc: LinkDescription) {
        self.node_table.update_link(from, to, link, desc);
        trace!("Schedule routing update");
        self.update_routing = true;
    }
}

type ListPeersResponderChannel = futures::channel::oneshot::Sender<Result<(u64, Vec<Peer>), Error>>;

/// NodeStateCallback implementation for a list_peers request from the main app.
struct ListPeersResponse {
    own_node_id: NodeId,
    responder: Option<ListPeersResponderChannel>,
}

impl ListPeersResponse {
    fn new(own_node_id: NodeId, responder: ListPeersResponderChannel) -> Box<ListPeersResponse> {
        Box::new(ListPeersResponse { own_node_id, responder: Some(responder) })
    }
}

impl NodeStateCallback for ListPeersResponse {
    fn trigger(&mut self, new_version: u64, node_table: &NodeTable) -> Result<(), Error> {
        let mut peers: Vec<_> = node_table
            .nodes()
            .map(|node_id| Peer {
                id: fidl_fuchsia_overnet_protocol::NodeId { id: node_id.0 },
                is_self: node_id == self.own_node_id,
                description: PeerDescription {
                    services: Some(node_table.node_services(node_id).to_vec()),
                },
            })
            .collect();
        peers.shuffle(&mut rand::thread_rng());
        info!("Respond to list_peers: {:?}", peers);
        match self
            .responder
            .take()
            .ok_or_else(|| format_err!("State callback called twice"))?
            .send(Ok((new_version, peers)))
        {
            Ok(_) => Ok(()),
            // Listener gone: response send ignored, but it already was.
            Err(_) => Ok(()),
        }
    }
}

struct SocketLink {
    router_id: LinkId,
    framer: StreamFramer,
    writes: Option<futures::io::WriteHalf<fidl::AsyncSocket>>,
}

#[derive(Clone, Copy, Debug)]
enum PhysLinkId<UpLinkID: Copy> {
    SocketLink(SaltedID),
    UpLink(UpLinkID),
}

struct NodeInner<Runtime: NodeRuntime> {
    /// Map of service name to provider.
    service_map: HashMap<String, ServiceProviderProxy>,
    /// Overnet router implementation.
    router: Router<PhysLinkId<Runtime::LinkId>, Runtime::Time>,
    /// overnetstack state for each active stream.
    streams: ShadowSlab<StreamBinding>,
    /// Table of known nodes and links in the mesh (generates routing data,
    /// and provides service discovery).
    node_table: NodeTable,
    /// Is a global state flush queued?
    flush_queued: bool,
    /// Is a routing update queued?
    routing_update_queued: bool,
    /// Generation counter for timeouts.
    timeout_key: u64,
    /// Map our externally visible link ids to real link ids.
    node_to_app_link_ids: BTreeMap<NodeLinkId, PhysLinkId<Runtime::LinkId>>,
    /// Attached socket links
    socket_links: SaltSlab<SocketLink>,
    /// Local node link ids
    next_node_link_id: u64,
    /// Our runtime
    runtime: Runtime,
}

/// Represents an Overnet node managed by this process
pub struct Node<Runtime: NodeRuntime> {
    inner: Rc<RefCell<NodeInner<Runtime>>>,
}

impl<Runtime: NodeRuntime + 'static> Node<Runtime> {
    /// Create a new instance of App
    pub fn new(runtime: Runtime, router_options: RouterOptions) -> Self {
        let router = Router::new_with_options(router_options);
        let streams_key = router.streams_key();
        Self {
            inner: Rc::new(RefCell::new(NodeInner {
                service_map: HashMap::new(),
                node_table: NodeTable::new(router.node_id()),
                router,
                streams: ShadowSlab::new(streams_key),
                timeout_key: 0,
                next_node_link_id: 1,
                flush_queued: false,
                routing_update_queued: false,
                node_to_app_link_ids: BTreeMap::new(),
                socket_links: SaltSlab::new(),
                runtime,
            })),
        }
    }

    /// Clone this node
    pub fn clone(&self) -> Self {
        Self { inner: self.inner.clone() }
    }

    /// Returns the globally unique identifier for this node
    pub fn id(&self) -> NodeId {
        self.inner.borrow().router.node_id()
    }

    /// Access the runtime
    pub fn with_runtime_mut<R>(&mut self, f: impl FnOnce(&mut Runtime) -> R) -> R {
        f(&mut self.inner.borrow_mut().runtime)
    }

    /// Implementation of ListPeers fidl method.
    pub async fn list_peers(self, last_seen_version: u64) -> Result<(u64, Vec<Peer>), Error> {
        let rx = {
            let this = &mut *self.inner.borrow_mut();
            let (tx, rx) = futures::channel::oneshot::channel();
            info!("Request list_peers last_seen_version={}", last_seen_version);
            this.node_table
                .post_query(last_seen_version, ListPeersResponse::new(this.router.node_id(), tx));
            self.clone().need_flush(this);
            rx
        };
        Ok(rx.await??)
    }

    /// Implementation of RegisterService fidl method.
    pub fn register_service(
        &self,
        service_name: String,
        provider: fidl::endpoints::ClientEnd<ServiceProviderMarker>,
    ) -> Result<(), Error> {
        let this = &mut *self.inner.borrow_mut();
        info!("Request register_service '{}'", service_name);
        if this.service_map.insert(service_name.clone(), provider.into_proxy()?).is_none() {
            info!("Publish new service '{}'", service_name);
            // This is a new service
            let services: Vec<String> = this.service_map.keys().cloned().collect();
            if let Err(e) = this.router.publish_node_description(services.clone()) {
                this.service_map.remove(&service_name);
                bail!(e)
            }
            this.node_table.update_node(this.router.node_id(), NodeDescription { services });
            self.clone().need_flush(this);
        }
        Ok(())
    }

    /// Implementation of ConnectToService fidl method.
    pub fn connect_to_service(
        &self,
        node_id: NodeId,
        service_name: &str,
        chan: Channel,
    ) -> Result<(), Error> {
        let this = &mut *self.inner.borrow_mut();
        info!("Request connect_to_service '{}' on {:?}", service_name, node_id);
        if node_id == this.router.node_id() {
            this.service_map
                .get(service_name)
                .ok_or_else(|| format_err!("Unknown service {}", service_name))?
                .connect_to_service(chan)?;
        } else {
            let stream_id = this.router.new_stream(node_id, service_name)?;
            let chan = Rc::new(AsyncChannel::from_channel(chan)?);
            this.streams.init(stream_id, StreamBinding::Channel(chan.clone()));
            this.runtime.spawn_local(self.clone().channel_reader(chan, stream_id));
            self.clone().need_flush(this);
        }
        Ok(())
    }

    /// Implementation of AttachToSocket fidl method.
    pub fn attach_socket_link(
        &self,
        connection_label: Option<String>,
        socket: fidl::Socket,
    ) -> Result<(), Error> {
        let this = &mut *self.inner.borrow_mut();
        let node_link_id = this.next_node_link_id.into();
        this.next_node_link_id += 1;
        this.runtime.spawn_local(self.clone().handshake_socket(
            node_link_id,
            connection_label,
            socket,
        ));
        Ok(())
    }

    async fn handshake_socket(
        self,
        node_link_id: NodeLinkId,
        connection_label: Option<String>,
        socket: fidl::Socket,
    ) {
        if let Err(e) = self.handshake_socket_inner(node_link_id, connection_label, socket).await {
            warn!("Socket handshake failed: {}", e);
        }
    }

    async fn handshake_socket_inner(
        self,
        node_link_id: NodeLinkId,
        connection_label: Option<String>,
        socket: fidl::Socket,
    ) -> Result<(), Error> {
        const GREETING_STRING: &str = "OVERNET SOCKET LINK";

        // Send first frame
        let mut framer = StreamFramer::new();
        let mut greeting = StreamSocketGreeting {
            magic_string: Some(GREETING_STRING.to_string()),
            node_id: Some(fidl_fuchsia_overnet_protocol::NodeId { id: self.id().0 }),
            connection_label,
        };
        let mut bytes = Vec::new();
        let mut handles = Vec::new();
        fidl::encoding::Encoder::encode(&mut bytes, &mut handles, &mut greeting)?;
        assert_eq!(handles.len(), 0);
        framer.queue_send(bytes.as_slice())?;
        let send = framer.take_sends();
        assert_eq!(send.len(), socket.write(&send)?);

        // Wait for first frame
        let (mut rx, tx) =
            futures::io::AsyncReadExt::split(fidl::AsyncSocket::from_socket(socket)?);
        let mut deframer = StreamDeframer::new();
        let mut buf = [0u8; 1024];
        let mut greeting_bytes = loop {
            let n = rx.read(&mut buf).await?;
            deframer.queue_recv(&buf[..n]);
            if let Some(greeting) = deframer.next_incoming_frame() {
                break greeting;
            }
        };
        let mut greeting = StreamSocketGreeting::empty();
        fidl::encoding::Decoder::decode_into(greeting_bytes.as_mut(), &mut [], &mut greeting)?;

        let node_id = match greeting {
            StreamSocketGreeting { magic_string: None, .. } => {
                bail!("Required magic string '{}' not present in greeting", GREETING_STRING)
            }
            StreamSocketGreeting { magic_string: Some(ref x), .. } if x != GREETING_STRING => {
                bail!("Expected magic string '{}' in greeting, got '{}'", GREETING_STRING, x)
            }
            StreamSocketGreeting { node_id: None, .. } => bail!("No node id in greeting"),
            StreamSocketGreeting { node_id: Some(n), .. } => n.id,
        };

        let (router_id, id) = {
            let this = &mut *self.inner.borrow_mut();
            let id = this.socket_links.insert(SocketLink {
                writes: Some(tx),
                router_id: LinkId::invalid(),
                framer,
            });
            let router_id = match self.new_link_inner(
                this,
                node_id.into(),
                node_link_id,
                PhysLinkId::SocketLink(id),
            ) {
                Err(e) => {
                    this.socket_links.remove(id);
                    bail!(e);
                }
                Ok(x) => {
                    this.socket_links.get_mut(id).unwrap().router_id = x;
                    x
                }
            };
            (router_id, id)
        };

        let result = self.socket_link_read_loop(router_id, deframer, rx).await;
        self.inner.borrow_mut().socket_links.remove(id);
        result
    }

    async fn socket_link_read_loop(
        &self,
        rtr_id: LinkId,
        mut deframer: StreamDeframer,
        mut rx: futures::io::ReadHalf<fidl::AsyncSocket>,
    ) -> Result<(), Error> {
        let mut buf = [0u8; 1024];
        loop {
            while let Some(mut frame) = deframer.next_incoming_frame() {
                self.queue_recv(rtr_id, frame.as_mut())?;
            }
            let n = rx.read(&mut buf).await?;
            if n == 0 {
                return Ok(());
            }
            deframer.queue_recv(&buf[..n]);
        }
    }

    /// Queue an incoming receive of `packet` from some link `link`
    pub fn queue_recv(&self, link_id: LinkId, packet: &mut [u8]) -> Result<(), Error> {
        let this = &mut *self.inner.borrow_mut();
        let r = this.router.queue_recv(link_id, packet);
        self.clone().need_flush(this);
        r
    }

    fn new_link_inner(
        &self,
        this: &mut NodeInner<Runtime>,
        peer: NodeId,
        node_link_id: NodeLinkId,
        up_id: PhysLinkId<Runtime::LinkId>,
    ) -> Result<LinkId, Error> {
        let r = this.router.new_link(peer, node_link_id, up_id);
        this.node_to_app_link_ids.insert(node_link_id, up_id);
        // TODO: move this to the router update_link path
        this.node_table.update_link(
            this.router.node_id(),
            peer,
            node_link_id,
            LinkDescription { round_trip_time: std::time::Duration::from_secs(10) },
        );
        self.clone().need_flush(this);
        r
    }

    /// Create a new link to `peer` with application link id `up_id`.
    pub fn new_link(&self, peer: NodeId, up_id: Runtime::LinkId) -> Result<LinkId, Error> {
        let this = &mut *self.inner.borrow_mut();
        let node_link_id = this.next_node_link_id.into();
        this.next_node_link_id += 1;
        self.new_link_inner(this, peer, node_link_id, PhysLinkId::UpLink(up_id))
    }

    /// Mention that some node exists
    pub fn mention_node(&self, node_id: NodeId) {
        let this = &mut *self.inner.borrow_mut();
        this.node_table.mention_node(node_id);
        self.clone().need_flush(this);
    }

    /// Mark that we need to flush state (perform read/write callbacks, examine expired timers).
    fn need_flush(self, this: &mut NodeInner<Runtime>) {
        if this.flush_queued {
            return;
        }
        this.flush_queued = true;
        let now = Runtime::Time::now();
        this.runtime.at(now, || self.flush());
    }
    /// Flush state.
    fn flush(self) {
        let node = self.clone();
        let mut this = &mut *self.inner.borrow_mut();
        assert!(this.flush_queued);
        this.flush_queued = false;

        let service_map = &this.service_map;
        let streams = &mut this.streams;
        let node_table = &mut this.node_table;
        let runtime = &mut this.runtime;
        let socket_links = &mut this.socket_links;

        let now = Runtime::Time::now();
        // Examine expired timers.
        this.router.update_time(now);
        // Pass up received messages.
        let mut receiver = Receiver::<Runtime> {
            node: node.clone(),
            service_map,
            streams,
            node_table,
            update_routing: false,
            runtime,
        };
        this.router.flush_recvs(&mut receiver);
        // Schedule update routing information if needed (we wait a little while to avoid thrashing)
        if receiver.update_routing && !this.routing_update_queued {
            this.routing_update_queued = true;
            let sched_node = node.clone();
            receiver.runtime.at(
                Runtime::Time::after(now, std::time::Duration::from_millis(100).into()),
                move || sched_node.perform_routing_update(),
            );
        }
        // Push down sent packets.
        this.router.flush_sends(|link, data| match link {
            PhysLinkId::UpLink(link) => runtime.send_on_link(*link, data),
            PhysLinkId::SocketLink(link_id) => {
                let link = socket_links
                    .get_mut(*link_id)
                    .ok_or_else(|| format_err!("Link {:?} not found", link_id))?;
                link.framer.queue_send(data)?;
                if let Some(tx) = link.writes.take() {
                    runtime.spawn_local(self.clone().flush_sends_to_socket_link(
                        *link_id,
                        tx,
                        link.framer.take_sends(),
                    ));
                }
                Ok(())
            }
        });
        // Trigger node table callbacks if they're queued (this will be responses to satisfied
        // ListPeers requests).
        this.node_table.trigger_callbacks();
        // Schedule an update to expire timers later, if necessary.
        this.timeout_key += 1;
        let timeout = this.router.next_timeout();
        trace!(
            "timeout key -> {}; timeout={:?} now={:?}",
            this.timeout_key,
            timeout,
            Runtime::Time::now()
        );
        if let Some(timeout) = timeout {
            let key = this.timeout_key;
            let node = node.clone();
            this.runtime.at(timeout, move || {
                let this = &mut *node.inner.borrow_mut();
                if this.timeout_key == key {
                    node.clone().need_flush(this);
                }
            });
        }
    }

    async fn flush_sends_to_socket_link(
        self,
        link_id: SaltedID,
        mut tx: futures::io::WriteHalf<fidl::AsyncSocket>,
        frame: Vec<u8>,
    ) {
        if frame.len() == 0 {
            let this = &mut *self.inner.borrow_mut();
            if let Some(link) = this.socket_links.get_mut(link_id) {
                link.writes = Some(tx);
            }
            return;
        }
        if let Err(e) = tx.write_all(frame.as_slice()).await {
            let this = &mut *self.inner.borrow_mut();
            warn!("Socket write failed: {}", e);
            this.socket_links.remove(link_id);
            return;
        }
        let this = &mut *self.inner.borrow_mut();
        if let Some(link) = this.socket_links.get_mut(link_id) {
            this.runtime.spawn_local(self.clone().flush_sends_to_socket_link(
                link_id,
                tx,
                link.framer.take_sends(),
            ));
        }
    }

    /// Perform a route table update.
    fn perform_routing_update(&self) {
        let mut this = &mut *self.inner.borrow_mut();
        assert!(this.routing_update_queued);
        this.routing_update_queued = false;
        trace!("UPDATE ROUTES");
        for (node_id, link_id) in this.node_table.build_routes() {
            trace!("  {:?} -> {:?}", node_id, link_id);
            if let Some(app_id) = this.node_to_app_link_ids.get(&link_id).copied() {
                let rtr_id = match app_id {
                    PhysLinkId::UpLink(app_id) => this.runtime.router_link_id(app_id),
                    PhysLinkId::SocketLink(id) => this
                        .socket_links
                        .get(id)
                        .map(|link| link.router_id)
                        .unwrap_or(LinkId::invalid()),
                };
                if let Err(e) = this.router.adjust_route(node_id, rtr_id) {
                    trace!("Failed updating route to {:?}: {:?}", node_id, e);
                }
            } else {
                trace!("Couldn't find appid");
            }
        }
    }

    /// Implements the read loop for getting data from a zircon channel and forwarding it to an
    /// Overnet stream.
    async fn channel_reader_inner(
        self,
        chan: Rc<AsyncChannel>,
        stream_id: StreamId,
    ) -> Result<(), Error> {
        let mut buf = fidl::MessageBuf::new();
        loop {
            chan.recv_msg(&mut buf).await?;
            let this = &mut *self.inner.borrow_mut();
            let (bytes, handles) = buf.split_mut();
            let mut send_handles = Vec::new();
            for handle in handles.iter() {
                send_handles.push(Runtime::handle_type(&handle)?);
            }
            let stream_ids = this.router.queue_send_channel_message(
                stream_id,
                std::mem::replace(bytes, Vec::new()),
                send_handles,
            )?;
            self.clone().need_flush(this);
            for (handle, stream_id) in handles.into_iter().zip(stream_ids.into_iter()) {
                match Runtime::handle_type(&handle).unwrap() {
                    SendHandle::Channel => {
                        let channel = Rc::new(AsyncChannel::from_channel(Channel::from_handle(
                            std::mem::replace(handle, Handle::invalid()),
                        ))?);
                        this.streams.init(stream_id, StreamBinding::Channel(channel.clone()));
                        this.runtime.spawn_local(self.clone().channel_reader(channel, stream_id));
                    }
                }
            }
        }
    }

    /// Wrapper for the above loop to handle errors 'gracefully'.
    async fn channel_reader(self, chan: Rc<AsyncChannel>, stream_id: StreamId) {
        if let Err(e) = self.channel_reader_inner(chan, stream_id).await {
            warn!("Channel reader failed: {:?}", e);
        }
    }
}
