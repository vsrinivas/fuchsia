// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fidl_fuchsia_net as fnet;
use net_declare::{fidl_subnet, std_socket_addr};
use netemul::RealmUdpSocket as _;
use netstack_testing_common::realms::{Netstack2, TestSandboxExt as _};

// NB: typically we prefer to panic in tests rather than returning `Result`;
// see https://fuchsia.dev/fuchsia-src/contribute/contributing-to-netstack/rust-patterns#prefer_panics.
// However, we return a `Result` here so we can use the try operator `?` for better readability.
#[fuchsia::test]
async fn test() -> Result<(), anyhow::Error> {
    let sandbox = netemul::TestSandbox::new()?;
    let network = sandbox.create_network("net").await?;

    const CLIENT_SUBNET: fnet::Subnet = fidl_subnet!("192.168.0.1/24");
    const SERVER_SUBNET: fnet::Subnet = fidl_subnet!("192.168.0.2/24");
    let client_addr = std_socket_addr!("192.168.0.1:1234");
    let server_addr = std_socket_addr!("192.168.0.2:8080");

    let client = sandbox.create_netstack_realm::<Netstack2, _>("client")?;
    let client_interface =
        client.join_network::<netemul::NetworkDevice, _>(&network, "client-ep").await?;
    client_interface.add_address_and_subnet_route(CLIENT_SUBNET).await?;
    let client_socket = fuchsia_async::net::UdpSocket::bind_in_realm(&client, client_addr).await?;

    let server = sandbox.create_netstack_realm::<Netstack2, _>("server")?;
    let server_interface =
        server.join_network::<netemul::NetworkDevice, _>(&network, "server-ep").await?;
    server_interface.add_address_and_subnet_route(SERVER_SUBNET).await?;
    let server_socket = fuchsia_async::net::UdpSocket::bind_in_realm(&server, server_addr).await?;

    const PAYLOAD: &'static str = "hello, world!";

    let client_fut = async {
        let written = client_socket.send_to(PAYLOAD.as_bytes(), server_addr).await.expect("sendto");
        assert_eq!(written, PAYLOAD.as_bytes().len());
    };
    let server_fut = async {
        let mut buf = [0u8; 1024];
        let (read, from) = server_socket.recv_from(&mut buf[..]).await.expect("recvfrom");
        assert_eq!(read, PAYLOAD.as_bytes().len());
        assert_eq!(&buf[..read], PAYLOAD.as_bytes());
        assert_eq!(from, client_addr);
    };

    let ((), ()) = futures_util::future::join(client_fut, server_fut).await;

    Ok(())
}
