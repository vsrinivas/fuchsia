// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Overnet daemon for Fuchsia

#![deny(missing_docs)]

mod mdns;
mod serial;

use anyhow::{Context as _, Error};
use argh::FromArgs;
use fidl_fuchsia_overnet::{
    MeshControllerRequest, MeshControllerRequestStream, ServiceConsumerRequest,
    ServiceConsumerRequestStream, ServicePublisherRequest, ServicePublisherRequestStream,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::future::{abortable, AbortHandle};
use futures::lock::Mutex;
use futures::prelude::*;
use overnet_core::{
    log_errors, LinkReceiver, NodeId, Router, RouterOptions, SimpleSecurityContext,
};
use std::collections::HashMap;
use std::net::{SocketAddr, SocketAddrV6};
use std::sync::Arc;

#[derive(FromArgs)]
/// Overnet.
struct Opts {
    #[argh(switch)]
    /// publish mdns service
    mdns_publish: bool,

    #[argh(switch)]
    /// connect to mdns services
    mdns_connect: bool,

    #[argh(switch)]
    /// open a udp port
    udp: bool,

    #[argh(option, default = "\"debug\".to_string()")]
    /// add serial links
    /// Can be 'none', 'all', or a specific path to a serial device.
    serial: String,
}

struct UdpSocketHolder {
    sock: Arc<fasync::net::UdpSocket>,
    abort_publisher: Option<AbortHandle>,
}

impl UdpSocketHolder {
    fn new(node_id: NodeId, publish_mdns: bool) -> Result<Self, Error> {
        let sock = std::net::UdpSocket::bind("[::]:0").context("Creating UDP socket")?;
        let port = sock.local_addr().context("Getting UDP local address")?.port();
        let sock = Arc::new(fasync::net::UdpSocket::from_socket(sock)?);
        let abort_publisher = if publish_mdns {
            let (publisher, abort_publisher) = abortable(mdns::publish(node_id, port));
            fasync::Task::local(async move {
                let _ = publisher.await;
            })
            .detach();
            Some(abort_publisher)
        } else {
            None
        };
        Ok(Self { sock, abort_publisher })
    }
}

impl Drop for UdpSocketHolder {
    fn drop(&mut self) {
        self.abort_publisher.take().map(|a| a.abort());
    }
}

// TODO: bundle send task with receiver and get memory management right here
type UdpLinks = Arc<Mutex<HashMap<SocketAddrV6, LinkReceiver>>>;

fn normalize_addr(addr: SocketAddr) -> SocketAddrV6 {
    match addr {
        SocketAddr::V6(a) => SocketAddrV6::new(*a.ip(), a.port(), 0, 0),
        SocketAddr::V4(a) => SocketAddrV6::new(a.ip().to_ipv6_mapped(), a.port(), 0, 0),
    }
}

/// UDP read inner loop.
async fn read_udp(udp_socket: Arc<UdpSocketHolder>, udp_links: UdpLinks) -> Result<(), Error> {
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
    node: Arc<Router>,
    udp_socket: Arc<UdpSocketHolder>,
    udp_links: UdpLinks,
) -> Result<(), Error> {
    let addr = normalize_addr(addr);
    let mut udp_links = udp_links.lock().await;
    if udp_links.get(&addr).is_none() {
        let (link_sender, link_receiver) = node
            .new_link(
                node_id,
                Box::new(move || {
                    Some(fidl_fuchsia_overnet_protocol::LinkConfig::Udp(
                        fidl_fuchsia_net::Ipv6SocketAddress {
                            address: fidl_fuchsia_net::Ipv6Address { addr: addr.ip().octets() },
                            port: addr.port(),
                            zone_index: addr.scope_id() as u64,
                        },
                    ))
                }),
            )
            .await?;
        udp_links.insert(addr, link_receiver);
        fasync::Task::local(log_errors(
            async move {
                let mut buf = [0u8; 1400];
                while let Some(n) = link_sender.next_send(&mut buf).await? {
                    println!("UDP_SEND to:{} len:{}", addr, n);
                    udp_socket.sock.clone().send_to(&buf[..n], addr.into()).await?;
                }
                Ok(())
            },
            "Failed sending UDP on link",
        ))
        .detach();
    }
    Ok(())
}

async fn run_service_publisher_server(
    node: Arc<Router>,
    stream: ServicePublisherRequestStream,
) -> Result<(), Error> {
    stream
        .map_err(Into::into)
        .try_for_each_concurrent(None, |request| {
            let node = node.clone();
            async move {
                match request {
                    ServicePublisherRequest::PublishService { service_name, provider, .. } => {
                        node.register_service(service_name, provider).await
                    }
                }
            }
        })
        .await
}

async fn run_service_consumer_server(
    node: Arc<Router>,
    stream: ServiceConsumerRequestStream,
) -> Result<(), Error> {
    let list_peers_context = Arc::new(node.new_list_peers_context());
    stream
        .map_err(Into::into)
        .try_for_each_concurrent(None, |request| {
            let node = node.clone();
            let list_peers_context = list_peers_context.clone();
            async move {
                match request {
                    ServiceConsumerRequest::ListPeers { responder, .. } => {
                        let mut peers = list_peers_context.list_peers().await?;
                        responder.send(&mut peers.iter_mut())?;
                        Ok(())
                    }
                    ServiceConsumerRequest::ConnectToService {
                        node: node_id,
                        service_name,
                        chan,
                        ..
                    } => node.connect_to_service(node_id.id.into(), &service_name, chan).await,
                }
            }
        })
        .await
}

async fn run_mesh_controller_server(
    node: Arc<Router>,
    stream: MeshControllerRequestStream,
) -> Result<(), Error> {
    stream
        .map_err(Into::into)
        .try_for_each_concurrent(None, |request| {
            let node = node.clone();
            async move {
                match request {
                    MeshControllerRequest::AttachSocketLink { socket, options, .. } => {
                        if let Err(e) = node.run_socket_link(socket, options).await {
                            log::warn!("Socket link failed: {:?}", e);
                        }
                        Ok(())
                    }
                }
            }
        })
        .await
}

enum IncomingService {
    ServiceConsumer(ServiceConsumerRequestStream),
    ServicePublisher(ServicePublisherRequestStream),
    MeshController(MeshControllerRequestStream),
    // ... more services here
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let opt: Opts = argh::from_env();

    fuchsia_syslog::init_with_tags(&["overnet"]).context("initialize logging")?;

    let mut fs = ServiceFs::new_local();
    let mut svc_dir = fs.dir("svc");
    svc_dir.add_fidl_service(IncomingService::ServiceConsumer);
    svc_dir.add_fidl_service(IncomingService::ServicePublisher);
    svc_dir.add_fidl_service(IncomingService::MeshController);

    fs.take_and_serve_directory_handle()?;

    let node = Router::new(
        RouterOptions::new()
            .export_diagnostics(fidl_fuchsia_overnet_protocol::Implementation::OvernetStack),
        Box::new(SimpleSecurityContext {
            node_cert: "/pkg/data/cert.crt",
            node_private_key: "/pkg/data/cert.key",
            root_cert: "/pkg/data/root.crt",
        }),
    )?;

    fasync::Task::local(log_errors(
        crate::serial::run_serial_link_handlers(Arc::downgrade(&node), opt.serial),
        "serial handler failed",
    ))
    .detach();

    if opt.udp {
        let udp_socket = Arc::new(UdpSocketHolder::new(node.node_id(), opt.mdns_publish)?);
        let udp_links: UdpLinks = Arc::new(Mutex::new(HashMap::new()));
        let (tx_addr, mut rx_addr) = futures::channel::mpsc::channel(1);
        fasync::Task::local(log_errors(
            read_udp(udp_socket.clone(), udp_links.clone()),
            "Error reading UDP socket",
        ))
        .detach();
        if opt.mdns_connect {
            fasync::Task::local(log_errors(mdns::subscribe(tx_addr), "MDNS Subscriber failed"))
                .detach();
        }
        let udp_node = node.clone();
        fasync::Task::local(log_errors(
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
        ))
        .detach();
    }

    fs.for_each_concurrent(None, move |svcreq| match svcreq {
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

// [START test_mod]
#[cfg(test)]
mod tests {
    use super::*;

    #[fasync::run_until_stalled(test)]
    async fn test_udplinks_hashmap() {
        //construct a router node
        let node = Router::new(
            RouterOptions::new(),
            Box::new(SimpleSecurityContext {
                node_cert: "/pkg/data/cert.crt",
                node_private_key: "/pkg/data/cert.key",
                root_cert: "/pkg/data/root.crt",
            }),
        )
        .unwrap();

        //let peer node id=9999 ,udp socket: [fe80::5054:ff:fe40:5763] port 56424
        //in function register_udp, arg: SocketAddr is construct at function endpoint6_to_socket in mdns.rs
        let socket_addr = SocketAddr::new(
            std::net::IpAddr::V6(std::net::Ipv6Addr::new(
                0xff80, 0x0000, 0x0000, 0x0000, 0x5054, 0x00ff, 0xfe40, 0x5763,
            )),
            56424,
        );
        let node_id = NodeId { 0: 9999 };

        //construct a peer link
        let (_link_sender, link_receiver) =
            node.new_link(node_id, Box::new(|| None)).await.unwrap();
        let udp_links: UdpLinks = Arc::new(Mutex::new(HashMap::new()));
        let mut udp_links = udp_links.lock().await;
        assert_eq!(udp_links.is_empty(), true);

        //insert (addr, link_receiver) hashmap
        let addr = normalize_addr(socket_addr);
        udp_links.insert(addr, link_receiver);
        assert_eq!(udp_links.is_empty(), false);

        //let socket_addr_v6 recv from udp package. flowinfo:0,scope_id:0
        let socket_addr_v6 = SocketAddrV6::new(
            std::net::Ipv6Addr::new(0xff80, 0x0000, 0x0000, 0x0000, 0x5054, 0x00ff, 0xfe40, 0x5763),
            56424,
            0,
            0,
        );
        assert_eq!(udp_links.get(&socket_addr_v6).is_none(), false);

        //let socket_addr_v6 recv from udp package. flowinfo:2,scope_id:0
        let socket_addr_v6 = SocketAddrV6::new(
            std::net::Ipv6Addr::new(0xff80, 0x0000, 0x0000, 0x0000, 0x5054, 0x00ff, 0xfe40, 0x5763),
            56424,
            2,
            0,
        );
        assert_eq!(udp_links.get(&socket_addr_v6).is_none(), true);
        assert_eq!(udp_links.get(&normalize_addr(SocketAddr::V6(socket_addr_v6))).is_none(), false);
    }
}
