// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl::endpoints::RequestStream;
use fidl_fuchsia_net_mdns::{
    Publication, PublicationResponder_Request, PublicationResponder_RequestStream, PublisherMarker,
    PublisherProxy, ServiceSubscriberRequest, ServiceSubscriberRequestStream, SubscriberMarker,
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

async fn publish_inner(node_id: NodeId, port: u16) -> Result<(), Error> {
    let (server, proxy) = zx::Channel::create()?;
    let server = fasync::Channel::from_channel(server)?;
    let mut stream = PublicationResponder_RequestStream::from_channel(server);

    let publisher = fuchsia_component::client::connect_to_service::<PublisherMarker>()?;

    fasync::spawn_local(connect_to_proxy(publisher, node_id, port, proxy));

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

/// Run main loop to publish a udp socket to mdns.
pub async fn publish(node_id: NodeId, port: u16) {
    if let Err(e) = publish_inner(node_id, port).await {
        log::warn!("mdns-publish-loop failed: {:?}", e);
    }
}

fn convert_ipv6_buffer(in_arr: [u8; 16]) -> [u16; 8] {
    let mut out_arr: [u16; 8] = [0; 8];

    for i in 0..8 {
        out_arr[i] = ((in_arr[2 * i] as u16) << 8) | (in_arr[i] as u16);
    }

    out_arr
}

fn fuchsia_to_rust_ipaddr(addr: fidl_fuchsia_net::IpAddress) -> std::net::IpAddr {
    match addr {
        fidl_fuchsia_net::IpAddress::Ipv4(addr) => std::net::IpAddr::V4(std::net::Ipv4Addr::new(
            addr.addr[0],
            addr.addr[1],
            addr.addr[2],
            addr.addr[3],
        )),
        fidl_fuchsia_net::IpAddress::Ipv6(addr) => {
            let addr = convert_ipv6_buffer(addr.addr);
            std::net::IpAddr::V6(std::net::Ipv6Addr::new(
                addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6], addr[7],
            ))
        }
    }
}

fn endpoint_to_socket(ep: fidl_fuchsia_net::Endpoint) -> std::net::SocketAddr {
    std::net::SocketAddr::new(fuchsia_to_rust_ipaddr(ep.addr), ep.port)
}

async fn subscribe_inner() -> Result<(), Error> {
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
                for endpoint in instance.endpoints.into_iter() {
                    crate::register_udp(
                        endpoint_to_socket(endpoint),
                        instance.instance.parse::<u64>()?.into(),
                    )?;
                }
                responder.send()?;
            }
            ServiceSubscriberRequest::OnInstanceChanged { instance, responder } => {
                log::info!("Changed: {:?}", instance);
                for endpoint in instance.endpoints.into_iter() {
                    crate::register_udp(
                        endpoint_to_socket(endpoint),
                        instance.instance.parse::<u64>()?.into(),
                    )?;
                }
                responder.send()?;
            }
            ServiceSubscriberRequest::OnInstanceLost { responder, .. } => {
                log::info!("Removed a thing");
                responder.send()?;
            }
        }
    }

    log::info!("Mdns subscriber finishes");

    Ok(())
}

/// Run main loop to look for overnet mdns advertisements and add them to the mesh.
pub async fn subscribe() {
    if let Err(e) = subscribe_inner().await {
        log::warn!("mdns-subscribe failed: {:?}", e);
    }
}
