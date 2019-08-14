// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Overnet daemon for Fuchsia

#![feature(async_await)]
#![deny(missing_docs)]

#[macro_use]
extern crate failure;
#[macro_use]
extern crate log;

mod mdns;

use core::{
    LinkDescription, LinkId, MessageReceiver, NodeDescription, NodeId, NodeLinkId,
    NodeStateCallback, NodeTable, Router, RouterOptions, RouterTime, SendHandle, StreamId,
    VersionCounter,
};
use failure::{Error, ResultExt};
use fidl_fuchsia_overnet::{
    OvernetListPeersResponder, OvernetRequest, OvernetRequestStream, Peer, ServiceProviderMarker,
    ServiceProviderProxy,
};
use fidl_fuchsia_overnet_protocol::PeerDescription;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_zircon as zx;
use futures::prelude::*;
use futures::ready;
use futures::task::{Context, Poll};
use rand::seq::SliceRandom;
use salt_slab::ShadowSlab;
use std::cell::RefCell;
use std::collections::{BTreeMap, HashMap};
use std::net::SocketAddr;
use std::os::unix::io::AsRawFd;
use std::pin::Pin;
use std::rc::Rc;
use zx::{AsHandleRef, HandleBased};

/// Binding of a stream to some communications structure.
enum StreamBinding {
    Channel(Rc<fasync::Channel>),
}

/// Identifier for a link as defined by overnetstack.
#[derive(Clone, Copy)]
enum AppLinkId {
    Udp(SocketAddr),
}

/// Adapter of fasync::Time to RouterTime for overnet's core library.
#[derive(PartialEq, PartialOrd, Eq, Ord, Clone, Copy, Debug)]
struct Time(fasync::Time);

impl RouterTime for Time {
    type Duration = zx::Duration;

    fn now() -> Self {
        Time(fasync::Time::now())
    }

    fn after(time: Self, duration: zx::Duration) -> Self {
        Self(time.0 + duration)
    }
}

/// Global state for overnetstack.
struct App {
    /// Map of service name to provider.
    service_map: HashMap<String, ServiceProviderProxy>,
    /// Overnet router implementation.
    router: Router<AppLinkId, Time>,
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
    node_to_app_link_ids: BTreeMap<NodeLinkId, AppLinkId>,

    // TODO(ctiller): This state should be moved out into its own file.
    /// Map socket addresses to udp link ids.
    udp_link_ids: HashMap<SocketAddr, LinkId>,
    /// UDP socket to communicate over.
    udp_socket: Option<fasync::net::EventedFd<std::net::UdpSocket>>,
}

thread_local! {
    // Always access via with_app_mut
    static APP: RefCell<App> = RefCell::new(App::new());
}

fn with_app_mut<R>(f: impl FnOnce(&mut App) -> R) -> R {
    APP.with(|rcapp| f(&mut rcapp.borrow_mut()))
}

/// Implements the read loop for getting data from a zircon channel and forwarding it to an overnet stream.
async fn channel_reader_inner(chan: Rc<fasync::Channel>, stream_id: StreamId) -> Result<(), Error> {
    let mut buf = fidl::MessageBuf::new();
    loop {
        chan.recv_msg(&mut buf).await?;
        with_app_mut(|app| -> Result<(), Error> {
            let (bytes, handles) = buf.split_mut();
            let mut send_handles = Vec::new();
            for handle in handles.iter() {
                let send_handle = match handle.basic_info()?.object_type {
                    zx::ObjectType::CHANNEL => SendHandle::Channel,
                    _ => bail!("Handle type not proxyable {:?}", handle.basic_info()?.object_type),
                };
                send_handles.push(send_handle);
            }
            let stream_ids = app.router.queue_send_channel_message(
                stream_id,
                std::mem::replace(bytes, Vec::new()),
                send_handles,
            )?;
            app.need_flush();
            for (handle, stream_id) in handles.into_iter().zip(stream_ids.into_iter()) {
                match handle.basic_info()?.object_type {
                    zx::ObjectType::CHANNEL => {
                        let channel =
                            Rc::new(fasync::Channel::from_channel(zx::Channel::from_handle(
                                std::mem::replace(handle, zx::Handle::invalid()),
                            ))?);
                        app.streams.init(stream_id, StreamBinding::Channel(channel.clone()));
                        fasync::spawn_local(channel_reader(channel, stream_id));
                    }
                    _ => unreachable!(),
                }
            }
            Ok(())
        })?;
    }
}

/// Wrapper for the above loop to handle errors 'gracefully'.
async fn channel_reader(chan: Rc<fasync::Channel>, stream_id: StreamId) {
    if let Err(e) = channel_reader_inner(chan, stream_id).await {
        warn!("Channel reader failed: {:?}", e);
    }
}

/// Implementation of core::MessageReceiver for overnetstack.
/// Maintains a small list of borrows from the App instance that creates it.
struct Receiver<'a> {
    service_map: &'a HashMap<String, ServiceProviderProxy>,
    streams: &'a mut ShadowSlab<StreamBinding>,
    node_table: &'a mut NodeTable,
    update_routing: bool,
}

impl<'a> Receiver<'a> {
    /// Given a stream_id create a channel to pass to an application as the outward facing interface.
    fn make_channel(&mut self, stream_id: StreamId) -> Result<zx::Channel, Error> {
        let (overnet_channel, app_channel) = zx::Channel::create()?;
        let overnet_channel = Rc::new(fasync::Channel::from_channel(overnet_channel)?);
        self.streams.init(stream_id, StreamBinding::Channel(overnet_channel.clone()));
        fasync::spawn_local(channel_reader(overnet_channel, stream_id));
        Ok(app_channel)
    }
}

impl<'a> MessageReceiver for Receiver<'a> {
    type Handle = zx::Handle;

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

    fn update_link(
        &mut self,
        from: NodeId,
        to: NodeId,
        link: NodeLinkId,
        version: VersionCounter,
        desc: LinkDescription,
    ) {
        self.node_table.update_link(from, to, link, version, desc);
        self.update_routing = true;
    }
}

/// NodeStateCallback implementation for a list_peers request from the main app.
struct ListPeersResponse {
    own_node_id: NodeId,
    responder: Option<OvernetListPeersResponder>,
}

impl ListPeersResponse {
    fn new(own_node_id: NodeId, responder: OvernetListPeersResponder) -> Box<ListPeersResponse> {
        Box::new(ListPeersResponse { own_node_id, responder: Some(responder) })
    }
}

impl NodeStateCallback for ListPeersResponse {
    fn trigger(&mut self, new_version: u64, node_table: &NodeTable) -> Result<(), Error> {
        let mut peers = Vec::new();
        for node_id in node_table.nodes() {
            peers.push(Peer {
                id: fidl_fuchsia_overnet_protocol::NodeId { id: node_id.0 },
                is_self: node_id == self.own_node_id,
                description: PeerDescription {
                    services: Some(node_table.node_services(node_id).to_vec()),
                },
            })
        }
        peers.shuffle(&mut rand::thread_rng());
        info!("Respond to list_peers: {:?}", peers);
        self.responder
            .take()
            .ok_or_else(|| format_err!("State callback called twice"))?
            .send(new_version, &mut peers.iter_mut())?;
        Ok(())
    }
}

async fn at(when: fasync::Time, f: impl FnOnce()) {
    fasync::Timer::new(when).await;
    f();
}

impl App {
    /// Create a new instance of App
    fn new() -> App {
        let router = Router::new_with_options(
            RouterOptions::new()
                .set_quic_server_key_file("/pkg/data/cert.key")
                .set_quic_server_cert_file("/pkg/data/cert.crt"),
        );
        App {
            service_map: HashMap::new(),
            node_table: NodeTable::new(router.node_id()),
            router,
            streams: ShadowSlab::new(),
            timeout_key: 0,
            flush_queued: false,
            routing_update_queued: false,
            node_to_app_link_ids: BTreeMap::new(),
            udp_link_ids: HashMap::new(),
            udp_socket: None,
        }
    }

    /// Implementation of ListPeers fidl method.
    fn list_peers(
        &mut self,
        last_seen_version: u64,
        responder: OvernetListPeersResponder,
    ) -> Result<(), Error> {
        info!("Request list_peers last_seen_version={}", last_seen_version);
        self.node_table.post_query(
            last_seen_version,
            ListPeersResponse::new(self.router.node_id(), responder),
        );
        self.need_flush();
        Ok(())
    }

    /// Implementation of RegisterService fidl method.
    fn register_service(
        &mut self,
        service_name: String,
        provider: fidl::endpoints::ClientEnd<ServiceProviderMarker>,
    ) -> Result<(), Error> {
        info!("Request register_service '{}'", service_name);
        if self.service_map.insert(service_name.clone(), provider.into_proxy()?).is_none() {
            info!("Publish new service '{}'", service_name);
            // This is a new service
            let services: Vec<String> = self.service_map.keys().cloned().collect();
            if let Err(e) = self.router.publish_node_description(services.clone()) {
                self.service_map.remove(&service_name);
                bail!(e)
            }
            self.node_table.update_node(self.router.node_id(), NodeDescription { services });
            self.need_flush();
        }
        Ok(())
    }

    /// Implementation of ConnectToService fidl method.
    fn connect_to_service(
        &mut self,
        node_id: NodeId,
        service_name: &str,
        chan: zx::Channel,
    ) -> Result<(), Error> {
        info!("Request connect_to_service '{}' on {:?}", service_name, node_id);
        if node_id == self.router.node_id() {
            self.service_map
                .get(service_name)
                .ok_or_else(|| format_err!("Unknown service {}", service_name))?
                .connect_to_service(chan)?;
        } else {
            let stream_id = self.router.new_stream(node_id, service_name)?;
            let chan = Rc::new(fasync::Channel::from_channel(chan)?);
            self.streams.init(stream_id, StreamBinding::Channel(chan.clone()));
            fasync::spawn_local(channel_reader(chan, stream_id));
            self.need_flush();
        }
        Ok(())
    }

    /// Mark that we need to flush state (perform read/write callbacks, examine expired timers).
    fn need_flush(&mut self) {
        if self.flush_queued {
            return;
        }
        self.flush_queued = true;
        let now = fasync::Time::now();
        fasync::spawn_local(at(now, || with_app_mut(|app| app.flush())));
    }

    /// Flush state.
    fn flush(&mut self) {
        assert!(self.flush_queued);
        self.flush_queued = false;

        let service_map = &self.service_map;
        let streams = &mut self.streams;
        let node_table = &mut self.node_table;

        let now = Time::now();
        // Examine expired timers.
        self.router.update_time(now);
        // Pass up received messages.
        let mut receiver = Receiver { service_map, streams, node_table, update_routing: false };
        self.router.flush_recvs(&mut receiver);
        // Push down sent packets.
        let udp_socket = self.udp_socket.as_mut();
        self.router.flush_sends(|link, data| {
            match link {
                AppLinkId::Udp(addr) => {
                    udp_socket
                        .as_ref()
                        .ok_or_else(|| format_err!("No udp socket configured"))?
                        .as_ref()
                        .send_to(data, *addr)?;
                }
            }
            Ok(())
        });
        // Schedule update routing information if needed (we wait a little while to avoid thrashing)
        if receiver.update_routing && !self.routing_update_queued {
            self.routing_update_queued = true;
            let when = fasync::Time::after(zx::Duration::from_millis(100));
            fasync::spawn_local(at(when, || with_app_mut(|app| app.routing_update())));
        }
        // Trigger node table callbacks if they're queued (this will be responses to satisfied
        // ListPeers requests).
        self.node_table.trigger_callbacks();
        // Schedule an update to expire timers later, if necessary.
        self.timeout_key += 1;
        let timeout = self.router.next_timeout();
        trace!("timeout key -> {}; timeout={:?} now={:?}", self.timeout_key, timeout, Time::now());
        if let Some(timeout) = timeout {
            let key = self.timeout_key;
            fasync::spawn_local(at(timeout.0, move || {
                with_app_mut(|app| {
                    if app.timeout_key == key {
                        app.need_flush();
                    }
                })
            }));
        }
    }

    /// Map overnetstack link id -> router link id.
    fn router_link_id(&mut self, app_id: AppLinkId) -> LinkId {
        match app_id {
            AppLinkId::Udp(addr) => {
                self.udp_link_ids.get(&addr).copied().unwrap_or(LinkId::invalid())
            }
        }
    }

    /// Perform a route table update.
    fn routing_update(&mut self) {
        assert!(self.routing_update_queued);
        self.routing_update_queued = false;
        for (node_id, link_id) in self.node_table.build_routes() {
            if let Some(app_id) = self.node_to_app_link_ids.get(&link_id).copied() {
                let rtr_id = self.router_link_id(app_id);
                if let Err(e) = self.router.adjust_route(node_id, rtr_id) {
                    warn!("Failed updating route to {:?}: {:?}", node_id, e);
                }
            }
        }
    }

    /// Poll udp socket for reads.
    pub fn async_recv_from(
        &self,
        buf: &mut [u8],
        cx: &mut Context<'_>,
    ) -> Poll<Result<(usize, SocketAddr), Error>> {
        if let Some(socket) = &self.udp_socket {
            ready!(fasync::net::EventedFd::poll_readable(&socket, cx))?;
            match socket.as_ref().recv_from(buf) {
                Err(e) => {
                    if e.kind() == std::io::ErrorKind::WouldBlock {
                        socket.need_read(cx);
                        Poll::Pending
                    } else {
                        Poll::Ready(Err(e.into()))
                    }
                }
                Ok((size, addr)) => Poll::Ready(Ok((size, addr))),
            }
        } else {
            Poll::Ready(Err(format_err!("No udp socket configured")))
        }
    }
}

/// Helper to asynchronously read the main udp socket.
struct ReadUdp<'a>(&'a mut [u8]);

impl<'a> Future for ReadUdp<'a> {
    type Output = Result<(usize, SocketAddr), Error>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let (received, addr) = ready!(with_app_mut(|app| app.async_recv_from(&mut self.0, cx)))?;
        Poll::Ready(Ok((received, addr)))
    }
}

/// UDP read inner loop.
async fn read_udp_inner() -> Result<(), Error> {
    let mut buf: [u8; 1500] = [0; 1500];
    loop {
        let (length, sender) = ReadUdp(&mut buf).await?;
        with_app_mut(|app| -> Result<(), Error> {
            if let Some(link_id) = app.udp_link_ids.get(&sender) {
                if let Err(e) = app.router.queue_recv(*link_id, &mut buf[..length]) {
                    warn!("Failed receiving packet from {:?}: {:?}", sender, e);
                    app.udp_link_ids.remove(&sender);
                }
                app.need_flush();
            } else {
                warn!("No link for received packet {:?}", sender);
            }
            Ok(())
        })?;
    }
}

/// Read UDP socket until closed, logging errors.
async fn read_udp() {
    if let Err(e) = read_udp_inner().await {
        warn!("UDP read loop failed: {:?}", e);
    }
}

/// Register a new UDP endpoint for some node_id.
fn register_udp(addr: SocketAddr, node_id: NodeId) -> Result<(), Error> {
    with_app_mut(|app| {
        app.node_table.mention_node(node_id);
        if app.udp_link_ids.get(&addr).is_none() {
            let rtr_id = app.router.new_link(node_id, AppLinkId::Udp(addr))?;
            app.udp_link_ids.insert(addr, rtr_id);
        }
        app.need_flush();
        Ok(())
    })
}

/// Service FIDL requests.
async fn run_overnet_server(mut stream: OvernetRequestStream) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("error running echo server")? {
        let result = with_app_mut(|app| match request {
            OvernetRequest::ListPeers { last_seen_version, responder, .. } => {
                app.list_peers(last_seen_version, responder)
            }
            OvernetRequest::RegisterService { service_name, provider, .. } => {
                app.register_service(service_name, provider)
            }
            OvernetRequest::ConnectToService { node, service_name, chan, .. } => {
                app.connect_to_service(node.id.into(), &service_name, chan)
            }
        });
        match result {
            Ok(()) => (),
            Err(e) => warn!("Error servicing request: {:?}", e),
        }
    }
    Ok(())
}

enum IncomingService {
    Overnet(OvernetRequestStream),
    // ... more services here
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["overnet"]).context("initialize logging")?;

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::Overnet);

    fs.take_and_serve_directory_handle()?;

    with_app_mut(|app| -> Result<(), Error> {
        let sock = std::net::UdpSocket::bind("0.0.0.0:0").context("Creating UDP socket")?;
        fasync::net::set_nonblock(sock.as_raw_fd())?;
        fasync::spawn_local(mdns::publish(
            app.router.node_id(),
            sock.local_addr().context("Getting UDP local address")?.port(),
        ));
        fasync::spawn_local(mdns::subscribe());
        app.udp_socket = Some(unsafe { fasync::net::EventedFd::new(sock) }?);
        fasync::spawn_local(read_udp());
        Ok(())
    })
    .context("Initializing UDP & MDNS")?;

    const MAX_CONCURRENT: usize = 10_000;
    fs.for_each_concurrent(MAX_CONCURRENT, |IncomingService::Overnet(stream)| {
        run_overnet_server(stream).unwrap_or_else(|e| trace!("{:?}", e))
    })
    .await;
    Ok(())
}
