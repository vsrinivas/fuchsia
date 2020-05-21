// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use net_declare::fidl_ip;
use netstack_testing_macros::endpoint_variants_test;

use crate::environments::*;
use crate::Result;

#[endpoint_variants_test]
async fn test_udp_socket<E: Endpoint>() -> Result {
    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let net = sandbox.create_network("net").await.context("failed to create network")?;
    let client = sandbox
        .create_netstack_environment::<Netstack2, _>("test_udp_socket_client")
        .context("failed to create client environment")?;

    const CLIENT_IP: fidl_fuchsia_net::IpAddress = fidl_ip!(192.168.0.2);
    const SERVER_IP: fidl_fuchsia_net::IpAddress = fidl_ip!(192.168.0.1);
    let _client_ep = client
        .join_network::<E, _>(
            &net,
            "client",
            InterfaceConfig::StaticIp(fidl_fuchsia_net_stack::InterfaceAddress {
                ip_address: CLIENT_IP,
                prefix_len: 24,
            }),
        )
        .await
        .context("client failed to join network")?;
    let server = sandbox
        .create_netstack_environment::<Netstack2, _>("test_udp_socket_server")
        .context("failed to create server environment")?;
    let _server_ep = server
        .join_network::<E, _>(
            &net,
            "server",
            InterfaceConfig::StaticIp(fidl_fuchsia_net_stack::InterfaceAddress {
                ip_address: SERVER_IP,
                prefix_len: 24,
            }),
        )
        .await
        .context("server failed to join network")?;

    let fidl_fuchsia_net_ext::IpAddress(client_addr) = CLIENT_IP.into();
    let client_addr = std::net::SocketAddr::new(client_addr, 1234);
    let fidl_fuchsia_net_ext::IpAddress(server_addr) = SERVER_IP.into();
    let server_addr = std::net::SocketAddr::new(server_addr, 8080);

    let client_sock = fuchsia_async::net::UdpSocket::bind_in_env(&client, client_addr)
        .await
        .context("failed to create client socket")?;

    let server_sock = fuchsia_async::net::UdpSocket::bind_in_env(&server, server_addr)
        .await
        .context("failed to create server socket")?;

    const PAYLOAD: &'static str = "Hello World";

    let client_fut = async move {
        let r =
            client_sock.send_to(PAYLOAD.as_bytes(), server_addr).await.context("sendto failed")?;
        assert_eq!(r, PAYLOAD.as_bytes().len());
        Result::Ok(())
    };
    let server_fut = async move {
        let mut buf = [0u8; 1024];
        let (r, from) = server_sock.recv_from(&mut buf[..]).await.context("recvfrom failed")?;
        assert_eq!(r, PAYLOAD.as_bytes().len());
        assert_eq!(&buf[..r], PAYLOAD.as_bytes());
        assert_eq!(from, client_addr);
        Result::Ok(())
    };

    let ((), ()) = futures::future::try_join(client_fut, server_fut).await?;

    Ok(())
}
