// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Overnet daemon for Fuchsia

#![deny(missing_docs)]

mod mdns;

use anyhow::{Context as _, Error};
use fidl_fuchsia_overnet::{
    MeshControllerRequest, MeshControllerRequestStream, OvernetListPeersResponder, OvernetRequest,
    OvernetRequestStream, ServiceConsumerListPeersResponder, ServiceConsumerRequest,
    ServiceConsumerRequestStream, ServicePublisherRequest, ServicePublisherRequestStream,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_zircon as zx;
use futures::future::{abortable, AbortHandle};
use futures::prelude::*;
use overnet_core::{LinkId, Node, NodeId, NodeOptions, NodeRuntime, RouterTime, SendHandle};
use std::cell::RefCell;
use std::collections::HashMap;
use std::net::{SocketAddr, SocketAddrV6};
use std::ops::Deref;
use std::rc::Rc;
use zx::AsHandleRef;

/// Identifier for a link as defined by overnetstack.
#[derive(Clone, Copy, Debug)]
enum AppLinkId {
    Udp(SocketAddrV6),
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

struct AppRuntime;

impl NodeRuntime for AppRuntime {
    type Time = Time;
    type LinkId = AppLinkId;
    const IMPLEMENTATION: fidl_fuchsia_overnet_protocol::Implementation =
        fidl_fuchsia_overnet_protocol::Implementation::OvernetStack;

    fn handle_type(handle: &zx::Handle) -> Result<SendHandle, Error> {
        match handle.basic_info()?.object_type {
            zx::ObjectType::CHANNEL => Ok(SendHandle::Channel),
            _ => {
                return Err(anyhow::format_err!(
                    "Handle type not proxyable {:?}",
                    handle.basic_info()?.object_type
                ))
            }
        }
    }

    fn spawn_local<F>(&mut self, future: F)
    where
        F: Future<Output = ()> + 'static,
    {
        fasync::spawn_local(future)
    }

    fn at(&mut self, t: Self::Time, f: impl FnOnce() + 'static) {
        fasync::spawn_local(at(t.0, f))
    }

    fn router_link_id(&self, id: AppLinkId) -> LinkId<overnet_core::PhysLinkId<AppLinkId>> {
        with_app_mut(|app| match id {
            AppLinkId::Udp(addr) => {
                app.udp_link_ids.get(&addr).copied().unwrap_or(LinkId::invalid())
            }
        })
    }

    fn send_on_link(&mut self, id: Self::LinkId, packet: &mut [u8]) -> Result<(), Error> {
        match id {
            AppLinkId::Udp(addr) => {
                println!("UDP_SEND to:{} len:{}", addr, packet.len());
                let sock = with_app_mut(|app| -> Result<_, Error> {
                    Ok(app
                        .udp_socket
                        .as_ref()
                        .ok_or_else(|| anyhow::format_err!("no udp socket"))?
                        .sock
                        .clone())
                })?;
                let sock = sock.deref().as_ref();
                if let Err(e) = sock.send_to(packet, addr) {
                    if e.kind() == std::io::ErrorKind::BrokenPipe {
                        log::warn!("BrokenPipe on UDP socket: let's make a new one");
                        with_app_mut(|app| {
                            app.udp_socket.take();
                            app.udp_socket = Some(UdpSocketHolder::new(app.node_id)?);
                            Ok(())
                        })
                    } else {
                        Err(e.into())
                    }
                } else {
                    Ok(())
                }
            }
        }
    }
}

struct UdpSocketHolder {
    sock: Rc<fasync::net::UdpSocket>,
    abort_publisher: AbortHandle,
}

impl UdpSocketHolder {
    fn new(node_id: NodeId) -> Result<Self, Error> {
        // Must not call with_app_mut here, as this is called from with_app_mut
        let sock = std::net::UdpSocket::bind("[::]:0").context("Creating UDP socket")?;
        let publisher =
            mdns::publish(node_id, sock.local_addr().context("Getting UDP local address")?.port());
        let sock = Rc::new(fasync::net::UdpSocket::from_socket(sock)?);
        let (publisher, abort_publisher) = abortable(publisher);
        fasync::spawn_local(async move {
            let _ = publisher.await;
        });
        Ok(Self { sock, abort_publisher })
    }
}

impl Drop for UdpSocketHolder {
    fn drop(&mut self) {
        self.abort_publisher.abort();
    }
}

/// Global state for overnetstack.
struct App {
    node_id: NodeId,
    node: Node<AppRuntime>,

    // TODO(ctiller): This state should be moved out into its own file.
    /// Map socket addresses to udp link ids.
    udp_link_ids: HashMap<SocketAddrV6, LinkId<overnet_core::PhysLinkId<AppLinkId>>>,
    /// UDP socket to communicate over.
    udp_socket: Option<UdpSocketHolder>,
}

thread_local! {
    // Always access via with_app_mut
    static APP: RefCell<App> = RefCell::new(App::new());
}

fn with_app_mut<R>(f: impl FnOnce(&mut App) -> R) -> R {
    APP.with(|rcapp| f(&mut rcapp.borrow_mut()))
}

async fn at(when: fasync::Time, f: impl FnOnce()) {
    fasync::Timer::new(when).await;
    f();
}

impl App {
    /// Create a new instance of App
    fn new() -> App {
        let node = Node::new(
            AppRuntime,
            NodeOptions::new()
                .set_quic_server_key_file(Box::new("/pkg/data/cert.key".to_string()))
                .set_quic_server_cert_file(Box::new("/pkg/data/cert.crt".to_string())),
        )
        .unwrap();
        App { node_id: node.id(), node, udp_link_ids: HashMap::new(), udp_socket: None }
    }
}

fn normalize_addr(addr: SocketAddr) -> SocketAddrV6 {
    match addr {
        SocketAddr::V6(a) => a,
        SocketAddr::V4(a) => SocketAddrV6::new(a.ip().to_ipv6_mapped(), a.port(), 0, 0),
    }
}

/// UDP read inner loop.
async fn read_udp_inner() -> Result<(), Error> {
    let mut buf: [u8; 1500] = [0; 1500];
    loop {
        let sock = with_app_mut(|app| -> Result<_, Error> {
            Ok(app
                .udp_socket
                .as_ref()
                .ok_or_else(|| anyhow::format_err!("No udp socket to read from"))?
                .sock
                .clone())
        })?;
        let (length, sender) = sock.recv_from(&mut buf).await?;
        println!("UDP_RECV from:{} len:{}", sender, length);
        let sender = normalize_addr(sender);
        with_app_mut(|app| -> Result<(), Error> {
            if let Some(link_id) = app.udp_link_ids.get(&sender) {
                app.node.queue_recv(*link_id, &mut buf[..length]);
            } else {
                log::warn!("No link for received packet {:?}", sender);
            }
            Ok(())
        })?;
    }
}

/// Read UDP socket until closed, logging errors.
async fn read_udp() {
    if let Err(e) = read_udp_inner().await {
        log::warn!("UDP read loop failed: {:?}", e);
    }
}

/// Register a new UDP endpoint for some node_id.
fn register_udp(addr: SocketAddr, node_id: NodeId) -> Result<(), Error> {
    with_app_mut(|app| {
        app.node.mention_node(node_id);
        let addr = normalize_addr(addr);
        if app.udp_link_ids.get(&addr).is_none() {
            let rtr_id = app.node.new_link(node_id, AppLinkId::Udp(addr))?;
            println!("register peer: {} node_id={:?} rtr_id={:?}", addr, node_id, rtr_id);
            app.udp_link_ids.insert(addr, rtr_id);
        }
        Ok(())
    })
}

trait ListPeersResponder {
    fn respond(
        self,
        peers: &mut dyn ExactSizeIterator<Item = &mut fidl_fuchsia_overnet::Peer>,
    ) -> Result<(), fidl::Error>;
}

impl ListPeersResponder for ServiceConsumerListPeersResponder {
    fn respond(
        self,
        peers: &mut dyn ExactSizeIterator<Item = &mut fidl_fuchsia_overnet::Peer>,
    ) -> Result<(), fidl::Error> {
        self.send(peers)
    }
}

impl ListPeersResponder for OvernetListPeersResponder {
    fn respond(
        self,
        peers: &mut dyn ExactSizeIterator<Item = &mut fidl_fuchsia_overnet::Peer>,
    ) -> Result<(), fidl::Error> {
        self.send(peers)
    }
}

async fn run_list_peers_inner(responder: impl ListPeersResponder) -> Result<(), Error> {
    let mut peers = with_app_mut(|app| app.node.clone().list_peers()).await?;
    responder.respond(&mut peers.iter_mut())?;
    Ok(())
}

async fn run_list_peers(responder: impl ListPeersResponder) {
    if let Err(e) = run_list_peers_inner(responder).await {
        log::warn!("List peers gets error: {:?}", e);
    }
}

async fn run_service_publisher_server(
    mut stream: ServicePublisherRequestStream,
) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("error running overnet server")? {
        let result = with_app_mut(|app| match request {
            ServicePublisherRequest::PublishService { service_name, provider, .. } => {
                app.node.register_service(service_name, provider)
            }
        });
        if let Err(e) = result {
            log::warn!("Error servicing request: {:?}", e)
        }
    }
    Ok(())
}

async fn run_service_consumer_server(
    mut stream: ServiceConsumerRequestStream,
) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("error running overnet server")? {
        let result = with_app_mut(|app| match request {
            ServiceConsumerRequest::ListPeers { responder, .. } => {
                fasync::spawn_local(run_list_peers(responder));
                Ok(())
            }
            ServiceConsumerRequest::ConnectToService { node, service_name, chan, .. } => {
                app.node.connect_to_service(node.id.into(), &service_name, chan)
            }
        });
        if let Err(e) = result {
            log::warn!("Error servicing request: {:?}", e);
        }
    }
    Ok(())
}

async fn run_mesh_controller_server(mut stream: MeshControllerRequestStream) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("error running overnet server")? {
        let result = with_app_mut(|app| match request {
            MeshControllerRequest::AttachSocketLink { socket, options, .. } => {
                app.node.attach_socket_link(socket, options)
            }
        });
        if let Err(e) = result {
            log::warn!("Error servicing request: {:?}", e);
        }
    }
    Ok(())
}

async fn run_legacy_overnet_server(mut stream: OvernetRequestStream) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("error running overnet server")? {
        let result = with_app_mut(|app| match request {
            OvernetRequest::PublishService { service_name, provider, .. } => {
                app.node.register_service(service_name, provider)
            }
            OvernetRequest::ListPeers { responder, .. } => {
                fasync::spawn_local(run_list_peers(responder));
                Ok(())
            }
            OvernetRequest::ConnectToService { node, service_name, chan, .. } => {
                app.node.connect_to_service(node.id.into(), &service_name, chan)
            }
        });
        if let Err(e) = result {
            log::warn!("Error servicing request: {:?}", e);
        }
    }
    Ok(())
}

enum IncomingService {
    ServiceConsumer(ServiceConsumerRequestStream),
    ServicePublisher(ServicePublisherRequestStream),
    MeshController(MeshControllerRequestStream),
    LegacyOvernet(OvernetRequestStream),
    // ... more services here
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["overnet"]).context("initialize logging")?;

    let mut fs = ServiceFs::new_local();
    let mut svc_dir = fs.dir("svc");
    svc_dir.add_fidl_service(IncomingService::ServiceConsumer);
    svc_dir.add_fidl_service(IncomingService::ServicePublisher);
    svc_dir.add_fidl_service(IncomingService::MeshController);
    svc_dir.add_fidl_service(IncomingService::LegacyOvernet);

    fs.take_and_serve_directory_handle()?;

    with_app_mut(|app| -> Result<(), Error> {
        app.udp_socket = Some(UdpSocketHolder::new(app.node.id())?);
        fasync::spawn_local(mdns::subscribe());
        fasync::spawn_local(read_udp());
        Ok(())
    })
    .context("Initializing UDP & MDNS")?;

    const MAX_CONCURRENT: usize = 10_000;
    fs.for_each_concurrent(MAX_CONCURRENT, |svcreq| match svcreq {
        IncomingService::MeshController(stream) => {
            run_mesh_controller_server(stream).unwrap_or_else(|e| log::trace!("{:?}", e)).boxed()
        }
        IncomingService::ServicePublisher(stream) => {
            run_service_publisher_server(stream).unwrap_or_else(|e| log::trace!("{:?}", e)).boxed()
        }
        IncomingService::ServiceConsumer(stream) => {
            run_service_consumer_server(stream).unwrap_or_else(|e| log::trace!("{:?}", e)).boxed()
        }
        IncomingService::LegacyOvernet(stream) => {
            run_legacy_overnet_server(stream).unwrap_or_else(|e| log::trace!("{:?}", e)).boxed()
        }
    })
    .await;
    Ok(())
}
