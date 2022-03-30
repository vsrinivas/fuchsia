// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_ext as fnet_ext;
use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use fidl_fuchsia_net_interfaces_ext as fnet_interfaces_ext;
use fidl_fuchsia_net_stack as fnet_stack;
use futures_util::{AsyncReadExt as _, AsyncWriteExt as _};
use net_declare::{fidl_ip, fidl_subnet, std_ip};
use netemul::{RealmTcpListener as _, RealmTcpStream as _};
use netstack_testing_common::realms::{Netstack2, TestSandboxExt as _};
use netstack_testing_macros::variants_test;
use test_case::test_case;

struct Setup {
    client_ip: std::net::IpAddr,
    client_subnet: fnet::Subnet,
    client_gateway: fnet::IpAddress,
    server_ip: std::net::IpAddr,
    server_subnet: fnet::Subnet,
    server_gateway: fnet::IpAddress,
    router_client_ip: fnet::Subnet,
    router_server_ip: fnet::Subnet,
    router_if_config: fnet_interfaces_admin::Configuration,
}

const PORT: u16 = 8080;
const REQUEST: &str = "hello from client";
const RESPONSE: &str = "hello from server";

#[variants_test]
#[test_case(
    Setup {
        client_ip: std_ip!("192.168.1.2"),
        client_subnet: fidl_subnet!("192.168.1.2/24"),
        client_gateway: fidl_ip!("192.168.1.1"),
        server_ip: std_ip!("192.168.0.2"),
        server_subnet: fidl_subnet!("192.168.0.2/24"),
        server_gateway: fidl_ip!("192.168.0.1"),
        router_client_ip: fidl_subnet!("192.168.1.1/24"),
        router_server_ip: fidl_subnet!("192.168.0.1/24"),
        router_if_config: fnet_interfaces_admin::Configuration {
            ipv4: Some(fnet_interfaces_admin::Ipv4Configuration {
                forwarding: Some(true),
                ..fnet_interfaces_admin::Ipv4Configuration::EMPTY
            }),
            ..fnet_interfaces_admin::Configuration::EMPTY
        },
    };
    "ipv4"
)]
#[test_case(
    Setup {
        client_ip: std_ip!("fd00:0:0:1::2"),
        client_subnet: fidl_subnet!("fd00:0:0:1::2/64"),
        client_gateway: fidl_ip!("fd00:0:0:1::1"),
        server_ip: std_ip!("fd00:0:0:2::2"),
        server_subnet: fidl_subnet!("fd00:0:0:2::2/64"),
        server_gateway: fidl_ip!("fd00:0:0:2::1"),
        router_client_ip: fidl_subnet!("fd00:0:0:1::1/64"),
        router_server_ip: fidl_subnet!("fd00:0:0:2::1/64"),
        router_if_config: fnet_interfaces_admin::Configuration {
            ipv6: Some(fnet_interfaces_admin::Ipv6Configuration {
                forwarding: Some(true),
                ..fnet_interfaces_admin::Ipv6Configuration::EMPTY
            }),
            ..fnet_interfaces_admin::Configuration::EMPTY
        },
    };
    "ipv6"
)]
async fn forwarding<E: netemul::Endpoint>(name: &str, setup: Setup) {
    let Setup {
        client_ip,
        client_subnet,
        client_gateway,
        server_ip,
        server_subnet,
        server_gateway,
        router_client_ip,
        router_server_ip,
        router_if_config,
    } = setup;

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let client_net = sandbox.create_network("client").await.expect("create network");
    let server_net = sandbox.create_network("server").await.expect("create network");
    let client = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_client", name))
        .expect("create realm");
    let server = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_server", name))
        .expect("create realm");
    let router = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_router", name))
        .expect("create realm");

    let client_iface = client
        .join_network::<E, _>(
            &client_net,
            "client-ep",
            &netemul::InterfaceConfig::StaticIp(client_subnet),
        )
        .await
        .expect("install interface in client netstack");
    let server_iface = server
        .join_network::<E, _>(
            &server_net,
            "server-ep",
            &netemul::InterfaceConfig::StaticIp(server_subnet),
        )
        .await
        .expect("install interface in server netstack");
    let router_client_iface = router
        .join_network::<E, _>(
            &client_net,
            "router-client-ep",
            &netemul::InterfaceConfig::StaticIp(router_client_ip),
        )
        .await
        .expect("install interface in router netstack");
    let router_server_iface = router
        .join_network::<E, _>(
            &server_net,
            "router-server-ep",
            &netemul::InterfaceConfig::StaticIp(router_server_ip),
        )
        .await
        .expect("install interface in router netstack");

    async fn add_default_gateway(
        realm: &netemul::TestRealm<'_>,
        interface: &netemul::TestInterface<'_>,
        gateway: fnet::IpAddress,
    ) {
        let unspecified_address = fnet_ext::IpAddress(match gateway {
            fnet::IpAddress::Ipv4(_) => std::net::IpAddr::V4(std::net::Ipv4Addr::UNSPECIFIED),
            fnet::IpAddress::Ipv6(_) => std::net::IpAddr::V6(std::net::Ipv6Addr::UNSPECIFIED),
        })
        .into();
        let stack =
            realm.connect_to_protocol::<fnet_stack::StackMarker>().expect("connect to protocol");
        stack
            .add_forwarding_entry(&mut fnet_stack::ForwardingEntry {
                subnet: fnet::Subnet { addr: unspecified_address, prefix_len: 0 },
                device_id: interface.id(),
                next_hop: Some(Box::new(gateway)),
                metric: 0,
            })
            .await
            .expect("call add forwarding entry")
            .expect("add forwarding entry");
    }
    add_default_gateway(&client, &client_iface, client_gateway).await;
    add_default_gateway(&server, &server_iface, server_gateway).await;

    async fn enable_forwarding(
        interface: &fnet_interfaces_ext::admin::Control,
        config: fnet_interfaces_admin::Configuration,
    ) {
        let _prev_config: fnet_interfaces_admin::Configuration = interface
            .set_configuration(config)
            .await
            .expect("call set configuration")
            .expect("set interface configuration");
    }
    enable_forwarding(router_client_iface.control(), router_if_config.clone()).await;
    enable_forwarding(router_server_iface.control(), router_if_config).await;

    let sockaddr = std::net::SocketAddr::from((server_ip, PORT));

    let client = async {
        let mut stream = fuchsia_async::net::TcpStream::connect_in_realm(&client, sockaddr)
            .await
            .expect("connect to server");
        let request = REQUEST.as_bytes();
        assert_eq!(stream.write(request).await.expect("write to stream"), request.len());
        stream.flush().await.expect("flush stream");

        let mut buffer = [0; 512];
        let read = stream.read(&mut buffer).await.expect("read from stream");
        let response = String::from_utf8_lossy(&buffer[0..read]);
        assert_eq!(response, RESPONSE, "got unexpected response from server: {}", response);
    };

    let listener = fuchsia_async::net::TcpListener::listen_in_realm(&server, sockaddr)
        .await
        .expect("bind to address");
    let server = async {
        let (_listener, mut stream, remote) =
            listener.accept().await.expect("accept incoming connection");
        assert_eq!(remote.ip(), client_ip);
        let mut buffer = [0; 512];
        let read = stream.read(&mut buffer).await.expect("read from stream");
        let request = String::from_utf8_lossy(&buffer[0..read]);
        assert_eq!(request, REQUEST, "got unexpected request from client: {}", request);

        let response = RESPONSE.as_bytes();
        assert_eq!(stream.write(response).await.expect("write to stream"), response.len());
        stream.flush().await.expect("flush stream");
    };

    futures_util::future::join(client, server).await;
}
