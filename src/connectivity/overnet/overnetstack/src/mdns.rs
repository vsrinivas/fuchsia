// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl::endpoints::RequestStream;
use fidl_fuchsia_net_mdns::{
    Media, Publication, PublicationResponder_Request, PublicationResponder_RequestStream,
    PublisherMarker, PublisherProxy, ServiceSubscriberRequest, ServiceSubscriberRequestStream,
    SubscriberMarker,
};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::prelude::*;
use overnet_core::NodeId;

const SERVICE_NAME: &str = "_rustic_overnet._udp.";

async fn connect_to_proxy(
    publisher: PublisherProxy,
    node_id: NodeId,
    port: u16,
    proxy: zx::Channel,
) {
    log::info!("Publish overnet service on port {}", port);

    match publisher
        .publish_service_instance(
            SERVICE_NAME,
            &format!("{}", node_id.0),
            Media::Wired | Media::Wireless,
            true,
            fidl::endpoints::ClientEnd::new(proxy),
        )
        .await
    {
        Ok(Ok(())) => log::warn!("Published overnet service"),
        Err(e) => log::warn!("FIDL failure: {:?}", e),
        Ok(Err(e)) => log::warn!("Mdns failure: {:?}", e),
    }
}

/// Run main loop to publish a udp socket to mdns.
pub async fn publish(node_id: NodeId, port: u16) -> Result<(), Error> {
    let (server, proxy) = zx::Channel::create()?;
    let server = fasync::Channel::from_channel(server)?;
    let mut stream = PublicationResponder_RequestStream::from_channel(server);

    let publisher = fuchsia_component::client::connect_to_service::<PublisherMarker>()?;

    fasync::Task::local(connect_to_proxy(publisher, node_id, port, proxy)).detach();

    log::info!("Waiting for mdns requests");

    while let Some(request) = stream.try_next().await? {
        let mut ok_publication = Publication {
            port,
            text: vec![],
            srv_priority: fidl_fuchsia_net_mdns::DEFAULT_SRV_PRIORITY,
            srv_weight: fidl_fuchsia_net_mdns::DEFAULT_SRV_WEIGHT,
            ptr_ttl: fidl_fuchsia_net_mdns::DEFAULT_PTR_TTL,
            srv_ttl: fidl_fuchsia_net_mdns::DEFAULT_SRV_TTL,
            txt_ttl: fidl_fuchsia_net_mdns::DEFAULT_TXT_TTL,
        };
        let ok_response = &mut ok_publication;
        let response = |ok| {
            if ok {
                Some(ok_response)
            } else {
                None
            }
        };

        match request {
            PublicationResponder_Request::OnPublication { responder, subtype: None, .. } => {
                responder.send(response(true))?;
            }
            PublicationResponder_Request::OnPublication { responder, subtype: Some(s), .. } => {
                responder.send(response(s == ""))?;
            }
        }
    }

    log::info!("Mdns publisher finishes");

    Ok(())
}

fn convert_ipv6_buffer(in_arr: [u8; 16]) -> [u16; 8] {
    let mut out_arr: [u16; 8] = [0; 8];

    for i in 0..8 {
        out_arr[i] = ((in_arr[2 * i] as u16) << 8) | (in_arr[2 * i + 1] as u16);
    }

    out_arr
}

fn fuchsia_to_rust_ipaddr4(addr: fidl_fuchsia_net::Ipv4Address) -> std::net::IpAddr {
    std::net::IpAddr::V4(std::net::Ipv4Addr::new(
        addr.addr[0],
        addr.addr[1],
        addr.addr[2],
        addr.addr[3],
    ))
}

fn fuchsia_to_rust_ipaddr6(addr: fidl_fuchsia_net::Ipv6Address) -> std::net::IpAddr {
    let addr = convert_ipv6_buffer(addr.addr);
    std::net::IpAddr::V6(std::net::Ipv6Addr::new(
        addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6], addr[7],
    ))
}

fn endpoint4_to_socket(ep: fidl_fuchsia_net::Ipv4SocketAddress) -> std::net::SocketAddr {
    std::net::SocketAddr::new(fuchsia_to_rust_ipaddr4(ep.address), ep.port)
}

fn endpoint6_to_socket(ep: fidl_fuchsia_net::Ipv6SocketAddress) -> std::net::SocketAddr {
    std::net::SocketAddr::new(fuchsia_to_rust_ipaddr6(ep.address), ep.port)
}

/// Run main loop to look for overnet mdns advertisements and add them to the mesh.
pub async fn subscribe(
    mut found: futures::channel::mpsc::Sender<(std::net::SocketAddr, NodeId)>,
) -> Result<(), Error> {
    let (server, proxy) = zx::Channel::create()?;
    let server = fasync::Channel::from_channel(server)?;
    let mut stream = ServiceSubscriberRequestStream::from_channel(server);

    log::info!("Query for overnet services");

    fuchsia_component::client::connect_to_service::<SubscriberMarker>()?
        .subscribe_to_service(SERVICE_NAME, fidl::endpoints::ClientEnd::new(proxy))?;

    log::info!("Wait for overnet services");

    while let Some(request) = stream.try_next().await? {
        match request {
            ServiceSubscriberRequest::OnInstanceDiscovered { instance, responder } => {
                log::info!("Discovered: {:?}", instance);
                let parsed_instance = instance
                    .instance
                    .ok_or(anyhow::format_err!("ServiceInstance.instance not supplied"))?
                    .parse::<u64>()?
                    .into();
                if let Some(ipv4_endpoint) = instance.ipv4_endpoint {
                    if found
                        .send((endpoint4_to_socket(ipv4_endpoint), parsed_instance))
                        .await
                        .is_err()
                    {
                        break;
                    }
                }
                if let Some(ipv6_endpoint) = instance.ipv6_endpoint {
                    if found
                        .send((endpoint6_to_socket(ipv6_endpoint), parsed_instance))
                        .await
                        .is_err()
                    {
                        break;
                    }
                }
                responder.send()?;
            }
            ServiceSubscriberRequest::OnInstanceChanged { instance, responder } => {
                log::info!("Changed: {:?}", instance);
                let parsed_instance = instance
                    .instance
                    .ok_or(anyhow::format_err!("ServiceInstance.instance not supplied"))?
                    .parse::<u64>()?
                    .into();
                if let Some(ipv4_endpoint) = instance.ipv4_endpoint {
                    if found
                        .send((endpoint4_to_socket(ipv4_endpoint), parsed_instance))
                        .await
                        .is_err()
                    {
                        break;
                    }
                }
                if let Some(ipv6_endpoint) = instance.ipv6_endpoint {
                    if found
                        .send((endpoint6_to_socket(ipv6_endpoint), parsed_instance))
                        .await
                        .is_err()
                    {
                        break;
                    }
                }
                responder.send()?;
            }
            ServiceSubscriberRequest::OnInstanceLost { responder, .. } => {
                log::info!("Removed a thing");
                responder.send()?;
            }
            ServiceSubscriberRequest::OnQuery { responder, .. } => {
                responder.send()?;
            }
        }
    }

    log::info!("Mdns subscriber finishes");

    Ok(())
}

// [START test_mod]
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn test_fuchsia_to_rust_ipaddr6() {
        //test example IPv6 address [fe80::5054:ff:fe40:5763]
        //fidl_fuchsia_net::Ipv6Address
        let ipv6_addr = fidl_fuchsia_net::Ipv6Address {
            addr: [
                0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x54, 0x00, 0xff, 0xfe, 0x40,
                0x57, 0x63,
            ],
        };

        //std::net::IpAddr
        let net_addr = std::net::IpAddr::V6(std::net::Ipv6Addr::new(
            0xff80, 0x0000, 0x0000, 0x0000, 0x5054, 0x00ff, 0xfe40, 0x5763,
        ));

        //expected:[fuchsia_to_rust_ipaddr6(ipv6_addr)] == [net_addr]
        assert_eq!(fuchsia_to_rust_ipaddr6(ipv6_addr), net_addr);
    }
}
// [END test_mod]
