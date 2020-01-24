// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Overnet daemon for Fuchsia

#![deny(missing_docs)]

mod mdns;

use anyhow::{Context as _, Error};
use fidl_fuchsia_overnet::{
    MeshControllerRequest, MeshControllerRequestStream, ServiceConsumerRequest,
    ServiceConsumerRequestStream, ServicePublisherRequest, ServicePublisherRequestStream,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::future::{abortable, AbortHandle};
use futures::lock::Mutex;
use futures::prelude::*;
use overnet_core::{log_errors, Link, NodeId, Router, RouterOptions};
use std::collections::HashMap;
use std::net::{SocketAddr, SocketAddrV6};
use std::rc::Rc;

struct UdpSocketHolder {
    sock: Rc<fasync::net::UdpSocket>,
    abort_publisher: AbortHandle,
}

impl UdpSocketHolder {
    fn new(node_id: NodeId) -> Result<Self, Error> {
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

type UdpLinks = Rc<Mutex<HashMap<SocketAddrV6, Rc<Link>>>>;

fn normalize_addr(addr: SocketAddr) -> SocketAddrV6 {
    match addr {
        SocketAddr::V6(a) => a,
        SocketAddr::V4(a) => SocketAddrV6::new(a.ip().to_ipv6_mapped(), a.port(), 0, 0),
    }
}

/// UDP read inner loop.
async fn read_udp(udp_socket: Rc<UdpSocketHolder>, udp_links: UdpLinks) -> Result<(), Error> {
    let mut buf = [0u8; 1500];
    loop {
        let sock = udp_socket.sock.clone();
        let (length, sender) = sock.recv_from(&mut buf).await?;
        println!("UDP_RECV from:{} len:{}", sender, length);
        let sender = normalize_addr(sender);
        let udp_links = udp_links.lock().await;
        if let Some(link) = udp_links.get(&sender) {
            if let Err(e) = link.received_packet(&mut buf[..length]).await {
                log::warn!("Failed receiving packet: {:?}", e);
            }
        } else {
            log::warn!("No link for received packet {:?}", sender);
        }
    }
}

/// Register a new UDP endpoint for some node_id.
async fn register_udp(
    addr: SocketAddr,
    node_id: NodeId,
    node: Rc<Router>,
    udp_socket: Rc<UdpSocketHolder>,
    udp_links: UdpLinks,
) -> Result<(), Error> {
    let addr = normalize_addr(addr);
    let mut udp_links = udp_links.lock().await;
    if udp_links.get(&addr).is_none() {
        let link = node.new_link(node_id).await?;
        udp_links.insert(addr, link.clone());
        fasync::spawn_local(log_errors(
            async move {
                let mut buf = [0u8; 2048];
                while let Some(n) = link.next_send(&mut buf).await? {
                    udp_socket.sock.clone().send_to(&buf[..n], addr.into()).await?;
                }
                Ok(())
            },
            "Failed sending UDP on link",
        ));
    }
    Ok(())
}

async fn run_service_publisher_server(
    node: Rc<Router>,
    mut stream: ServicePublisherRequestStream,
) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("error running overnet server")? {
        let result = match request {
            ServicePublisherRequest::PublishService { service_name, provider, .. } => {
                node.register_service(service_name, provider).await
            }
        };
        if let Err(e) = result {
            log::warn!("Error servicing request: {:?}", e)
        }
    }
    Ok(())
}

async fn run_service_consumer_server(
    node: Rc<Router>,
    mut stream: ServiceConsumerRequestStream,
) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("error running overnet server")? {
        let result = match request {
            ServiceConsumerRequest::ListPeers { responder, .. } => {
                node.list_peers(Box::new(|mut peers| {
                    if let Err(e) = responder.send(&mut peers.iter_mut()) {
                        log::warn!("Failed sending list peers response: {}", e);
                    }
                }))
                .await
            }
            ServiceConsumerRequest::ConnectToService {
                node: node_id, service_name, chan, ..
            } => node.connect_to_service(node_id.id.into(), &service_name, chan).await,
        };
        if let Err(e) = result {
            log::warn!("Error servicing request: {:?}", e);
        }
    }
    Ok(())
}

async fn run_mesh_controller_server(
    node: Rc<Router>,
    mut stream: MeshControllerRequestStream,
) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("error running overnet server")? {
        let result = match request {
            MeshControllerRequest::AttachSocketLink { socket, options, .. } => {
                node.attach_socket_link(socket, options)
            }
        };
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

    fs.take_and_serve_directory_handle()?;

    let node = Router::new(
        RouterOptions::new()
            .set_quic_server_key_file(Box::new("/pkg/data/cert.key".to_string()))
            .set_quic_server_cert_file(Box::new("/pkg/data/cert.crt".to_string()))
            .export_diagnostics(fidl_fuchsia_overnet_protocol::Implementation::OvernetStack),
    )?;

    let udp_socket = Rc::new(UdpSocketHolder::new(node.node_id())?);
    let udp_links: UdpLinks = Rc::new(Mutex::new(HashMap::new()));
    let (tx_addr, mut rx_addr) = futures::channel::mpsc::channel(1);
    fasync::spawn_local(log_errors(mdns::subscribe(tx_addr), "MDNS Subscriber failed"));
    fasync::spawn_local(log_errors(
        read_udp(udp_socket.clone(), udp_links.clone()),
        "Error reading UDP socket",
    ));
    let udp_node = node.clone();
    fasync::spawn_local(log_errors(
        async move {
            while let Some((addr, node_id)) = rx_addr.next().await {
                register_udp(
                    addr,
                    node_id,
                    udp_node.clone(),
                    udp_socket.clone(),
                    udp_links.clone(),
                )
                .await?;
            }
            Ok(())
        },
        "Error registering links",
    ));

    const MAX_CONCURRENT: usize = 10_000;
    fs.for_each_concurrent(MAX_CONCURRENT, move |svcreq| match svcreq {
        IncomingService::MeshController(stream) => run_mesh_controller_server(node.clone(), stream)
            .unwrap_or_else(|e| log::trace!("{:?}", e))
            .boxed_local(),
        IncomingService::ServicePublisher(stream) => {
            run_service_publisher_server(node.clone(), stream)
                .unwrap_or_else(|e| log::trace!("{:?}", e))
                .boxed_local()
        }
        IncomingService::ServiceConsumer(stream) => {
            run_service_consumer_server(node.clone(), stream)
                .unwrap_or_else(|e| log::trace!("{:?}", e))
                .boxed_local()
        }
    })
    .await;
    Ok(())
}
