// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_filter as fnetfilter;
use fuchsia_async::{DurationExt as _, TimeoutExt as _};
use futures::{
    io::AsyncReadExt as _, io::AsyncWriteExt as _, FutureExt as _, StreamExt, TryFutureExt as _,
};
use net_declare::{fidl_mac, fidl_subnet};
use netemul::{RealmTcpListener as _, RealmTcpStream as _, RealmUdpSocket as _};
use netfilter::FidlReturn as _;
use netstack_testing_common::realms::{Netstack2, TestSandboxExt as _};
use netstack_testing_common::{
    ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT, ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
};
use std::ops::RangeInclusive;

pub const CLIENT_IPV4_SUBNET: fnet::Subnet = fidl_subnet!("192.168.0.2/24");
pub const SERVER_IPV4_SUBNET: fnet::Subnet = fidl_subnet!("192.168.0.1/24");
pub const CLIENT_MAC_ADDRESS: fnet::MacAddress = fidl_mac!("02:00:00:00:00:01");
pub const SERVER_MAC_ADDRESS: fnet::MacAddress = fidl_mac!("02:00:00:00:00:02");

pub const CLIENT_PORT: u16 = 1234;
pub const SERVER_PORT: u16 = 8080;

pub const CLIENT_PAYLOAD: &'static str = "Enjoy your food!";
pub const SERVER_PAYLOAD: &'static str = "Thanks, you too...";

#[derive(Copy, Clone)]
pub enum ExpectedTraffic {
    ClientToServerOnly,
    TwoWay,
}

pub struct Test {
    pub proto: fnetfilter::SocketProtocol,
    pub client_updates: Option<Vec<fnetfilter::Rule>>,
    pub server_updates: Option<Vec<fnetfilter::Rule>>,
    pub expected_traffic: ExpectedTraffic,
}

pub async fn run_udp_socket_test(
    server: &netemul::TestRealm<'_>,
    server_addr: fidl_fuchsia_net::IpAddress,
    server_port: u16,
    client: &netemul::TestRealm<'_>,
    client_addr: fidl_fuchsia_net::IpAddress,
    client_port: u16,
    expected_traffic: ExpectedTraffic,
) {
    let fidl_fuchsia_net_ext::IpAddress(client_addr) =
        fidl_fuchsia_net_ext::IpAddress::from(client_addr);
    let client_addr = std::net::SocketAddr::new(client_addr, client_port);

    let fidl_fuchsia_net_ext::IpAddress(server_addr) =
        fidl_fuchsia_net_ext::IpAddress::from(server_addr);
    let server_addr = std::net::SocketAddr::new(server_addr, server_port);

    let client_sock = fuchsia_async::net::UdpSocket::bind_in_realm(client, client_addr)
        .await
        .expect("failed to create client socket");
    let client_addr =
        client_sock.local_addr().expect("failed to get local address from client socket");

    let server_sock = fuchsia_async::net::UdpSocket::bind_in_realm(server, server_addr)
        .await
        .expect("failed to create server socket");

    let server_fut = async move {
        let mut buf = [0u8; 1024];
        let (r, from) = server_sock.recv_from(&mut buf[..]).await.expect("recvfrom failed");
        assert_eq!(r, CLIENT_PAYLOAD.as_bytes().len());
        assert_eq!(&buf[..r], CLIENT_PAYLOAD.as_bytes());
        assert_eq!(from, client_addr);
        let r = server_sock
            .send_to(SERVER_PAYLOAD.as_bytes(), client_addr)
            .await
            .expect("send to failed");
        assert_eq!(r, SERVER_PAYLOAD.as_bytes().len());
    };

    let client_fut = async move {
        let r = client_sock
            .send_to(CLIENT_PAYLOAD.as_bytes(), server_addr)
            .await
            .expect("sendto failed");
        assert_eq!(r, CLIENT_PAYLOAD.as_bytes().len());

        let mut buf = [0u8; 1024];
        match expected_traffic {
            ExpectedTraffic::ClientToServerOnly => {
                match client_sock
                    .recv_from(&mut buf[..])
                    .map_ok(Some)
                    .on_timeout(ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT.after_now(), || Ok(None))
                    .await
                    .expect("recvfrom failed")
                {
                    Some((r, from)) => {
                        panic!("unexpectedly received packet {:?} from {:?}", &buf[..r], from)
                    }
                    None => (),
                }
            }
            ExpectedTraffic::TwoWay => {
                let (r, from) = client_sock
                    .recv_from(&mut buf[..])
                    .map(|r| r.expect("recvfrom failed"))
                    .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
                        panic!(
                            "timed out waiting for packet from server after {:?}",
                            ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT
                        );
                    })
                    .await;
                assert_eq!(r, SERVER_PAYLOAD.as_bytes().len());
                assert_eq!(&buf[..r], SERVER_PAYLOAD.as_bytes());
                assert_eq!(from, server_addr);
            }
        }
    };
    let ((), ()) = futures::future::join(server_fut, client_fut).await;
}

pub async fn run_tcp_socket_test(
    server: &netemul::TestRealm<'_>,
    server_addr: fidl_fuchsia_net::IpAddress,
    server_port: u16,
    client: &netemul::TestRealm<'_>,
    client_addr: fidl_fuchsia_net::IpAddress,
    client_port: u16,
    expected_traffic: ExpectedTraffic,
) {
    let fidl_fuchsia_net_ext::IpAddress(client_addr) =
        fidl_fuchsia_net_ext::IpAddress::from(client_addr);
    let client_addr = std::net::SocketAddr::new(client_addr, client_port);

    let fidl_fuchsia_net_ext::IpAddress(server_addr) =
        fidl_fuchsia_net_ext::IpAddress::from(server_addr);
    let server_addr = std::net::SocketAddr::new(server_addr, server_port);

    let listener =
        fuchsia_async::net::TcpListener::listen_in_realm_with(server, server_addr, |sock| {
            sock.set_reuse_port(true).context("failed to set reuse port")
        })
        .await
        .expect("failed to create server socket");

    let server_fut = async move {
        match expected_traffic {
            ExpectedTraffic::ClientToServerOnly => {
                match listener
                    .accept()
                    .map_ok(Some)
                    .on_timeout(ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT.after_now(), || Ok(None))
                    .await
                    .expect("failed to accept a connection")
                {
                    Some(_stream) => panic!("unexpectedly connected successfully"),
                    None => (),
                }
            }
            ExpectedTraffic::TwoWay => {
                let (_listener, mut stream, from) = listener
                    .accept()
                    .map(|r| r.expect("accept failed"))
                    .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
                        panic!(
                            "timed out waiting for a connection after {:?}",
                            ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT
                        );
                    })
                    .await;

                let mut buf = [0u8; 1024];
                let read_count =
                    stream.read(&mut buf).await.expect("read from tcp server stream failed");

                assert_eq!(from.ip(), client_addr.ip());
                assert_eq!(read_count, CLIENT_PAYLOAD.as_bytes().len());
                assert_eq!(&buf[..read_count], CLIENT_PAYLOAD.as_bytes());

                let write_count = stream
                    .write(SERVER_PAYLOAD.as_bytes())
                    .await
                    .expect("write to tcp server stream failed");
                assert_eq!(write_count, SERVER_PAYLOAD.as_bytes().len());
            }
        }
    };

    let client_fut = async move {
        match expected_traffic {
            ExpectedTraffic::ClientToServerOnly => {
                match fuchsia_async::net::TcpStream::connect_in_realm(client, server_addr)
                    .map_ok(Some)
                    .on_timeout(ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT.after_now(), || Ok(None))
                    .await
                    .expect("failed to create client socket")
                {
                    Some(_stream) => panic!("unexpectedly connected successfully"),
                    None => (),
                }
            }
            ExpectedTraffic::TwoWay => {
                let mut stream =
                    fuchsia_async::net::TcpStream::connect_in_realm(client, server_addr)
                        .map(|r| r.expect("connect_in_realm failed"))
                        .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
                            panic!(
                                "timed out waiting for a connection after {:?}",
                                ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT
                            );
                        })
                        .await;

                let write_count = stream
                    .write(CLIENT_PAYLOAD.as_bytes())
                    .await
                    .expect("write to tcp client stream failed");

                assert_eq!(write_count, CLIENT_PAYLOAD.as_bytes().len());

                let mut buf = [0u8; 1024];
                let read_count =
                    stream.read(&mut buf).await.expect("read from tcp client stream failed");

                assert_eq!(read_count, SERVER_PAYLOAD.as_bytes().len());
                assert_eq!(&buf[..read_count], SERVER_PAYLOAD.as_bytes());
            }
        }
    };

    let ((), ()) = futures::future::join(client_fut, server_fut).await;
}

pub async fn run_socket_test(
    proto: fnetfilter::SocketProtocol,
    server: &netemul::TestRealm<'_>,
    server_addr: fidl_fuchsia_net::IpAddress,
    server_port: u16,
    client: &netemul::TestRealm<'_>,
    client_addr: fidl_fuchsia_net::IpAddress,
    client_port: u16,
    expected_traffic: ExpectedTraffic,
) {
    match proto {
        fnetfilter::SocketProtocol::Udp => {
            run_udp_socket_test(
                server,
                server_addr,
                server_port,
                client,
                client_addr,
                client_port,
                expected_traffic,
            )
            .await
        }
        fnetfilter::SocketProtocol::Tcp => {
            run_tcp_socket_test(
                server,
                server_addr,
                server_port,
                client,
                client_addr,
                client_port,
                expected_traffic,
            )
            .await
        }
        proto => panic!("unexpected protocol {:?}", proto),
    }
}

pub async fn test_filter<E: netemul::Endpoint>(name: &str, test: Test) {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let net = sandbox.create_network("net").await.expect("failed to create network");

    let client = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_client", name))
        .expect("failed to create client realm");
    let client_ep = client
        .join_network_with(
            &net,
            "client",
            E::make_config(netemul::DEFAULT_MTU, Some(CLIENT_MAC_ADDRESS)),
            Default::default(),
        )
        .await
        .expect("client failed to join network");
    client_ep.add_address_and_subnet_route(CLIENT_IPV4_SUBNET).await.expect("configure address");
    let client_filter = client
        .connect_to_protocol::<fnetfilter::FilterMarker>()
        .expect("client failed to connect to filter service");

    let server = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_server", name))
        .expect("failed to create server realm");
    let server_ep = server
        .join_network_with(
            &net,
            "server",
            E::make_config(netemul::DEFAULT_MTU, Some(SERVER_MAC_ADDRESS)),
            Default::default(),
        )
        .await
        .expect("server failed to join network");
    server_ep.add_address_and_subnet_route(SERVER_IPV4_SUBNET).await.expect("configure address");

    // Put client and server in each other's neighbor table. We've observed
    // flakes in CQ due to ARP timeouts and ARP resolution is immaterial to the
    // tests we run here.
    let () = futures::stream::iter([
        (&server, &server_ep, CLIENT_IPV4_SUBNET.addr, SERVER_MAC_ADDRESS),
        (&client, &client_ep, SERVER_IPV4_SUBNET.addr, CLIENT_MAC_ADDRESS),
    ])
    .for_each_concurrent(None, |(realm, ep, addr, mac)| {
        realm.add_neighbor_entry(ep.id(), addr, mac).map(|r| r.expect("add_neighbor_entry"))
    })
    .await;

    let server_filter = server
        .connect_to_protocol::<fnetfilter::FilterMarker>()
        .expect("server failed to connect to filter service");

    let Test { proto, client_updates, server_updates, expected_traffic } = test;

    // Initial check (no filters set).
    let () = client_filter
        .enable_interface(client_ep.id())
        .await
        .transform_result()
        .expect("error enabling filter on client");
    let () = server_filter
        .enable_interface(server_ep.id())
        .await
        .transform_result()
        .expect("error enabling filter on server");
    let () = run_socket_test(
        proto,
        &server,
        SERVER_IPV4_SUBNET.addr,
        SERVER_PORT,
        &client,
        CLIENT_IPV4_SUBNET.addr,
        CLIENT_PORT,
        ExpectedTraffic::TwoWay,
    )
    .await;

    // Set the filters and do the test.
    let (_rules, mut server_generation) = server_filter
        .get_rules()
        .await
        .transform_result()
        .expect("failed to get server's filter rules");
    let (_rules, mut client_generation) = client_filter
        .get_rules()
        .await
        .transform_result()
        .expect("failed to get client's filter rules");
    if let Some(mut updates) = client_updates {
        let () = client_filter
            .update_rules(&mut updates.iter_mut(), client_generation)
            .await
            .transform_result()
            .expect("failed to update client's filter rules");
        client_generation += 1;
    }
    if let Some(mut updates) = server_updates {
        let () = server_filter
            .update_rules(&mut updates.iter_mut(), server_generation)
            .await
            .transform_result()
            .expect("failed to update server's filter rules");
        server_generation += 1;
    }
    let () = run_socket_test(
        proto,
        &server,
        SERVER_IPV4_SUBNET.addr,
        SERVER_PORT,
        &client,
        CLIENT_IPV4_SUBNET.addr,
        CLIENT_PORT,
        expected_traffic,
    )
    .await;

    // Disable the filters on the interface and expect full connectivity.
    let () = client_filter
        .disable_interface(client_ep.id())
        .await
        .transform_result()
        .expect("error disabling filter on client");
    let () = server_filter
        .disable_interface(server_ep.id())
        .await
        .transform_result()
        .expect("error disabling filter on server");
    let () = run_socket_test(
        proto,
        &server,
        SERVER_IPV4_SUBNET.addr,
        SERVER_PORT,
        &client,
        CLIENT_IPV4_SUBNET.addr,
        CLIENT_PORT,
        ExpectedTraffic::TwoWay,
    )
    .await;

    // Reset and enable filters and expect full connectivity.
    let () = client_filter
        .enable_interface(client_ep.id())
        .await
        .transform_result()
        .expect("error re-enabling filter on client");
    let () = server_filter
        .enable_interface(server_ep.id())
        .await
        .transform_result()
        .expect("error re-enabling filter on server");
    let () = server_filter
        .update_rules(&mut Vec::new().iter_mut(), server_generation)
        .await
        .transform_result()
        .expect("failed to reset client's filter rules");
    let () = client_filter
        .update_rules(&mut Vec::new().iter_mut(), client_generation)
        .await
        .transform_result()
        .expect("failed to reset client's filter rules");
    run_socket_test(
        proto,
        &server,
        SERVER_IPV4_SUBNET.addr,
        SERVER_PORT,
        &client,
        CLIENT_IPV4_SUBNET.addr,
        CLIENT_PORT,
        ExpectedTraffic::TwoWay,
    )
    .await
}

/// Tests how the filter on server drops the outgoing traffic from server.
pub fn server_outgoing_drop_test(
    proto: fnetfilter::SocketProtocol,
    src_subnet: Option<fnet::Subnet>,
    src_subnet_invert_match: bool,
    dst_subnet: Option<fnet::Subnet>,
    dst_subnet_invert_match: bool,
    src_port_range: RangeInclusive<u16>,
    dst_port_range: RangeInclusive<u16>,
    expected_traffic: ExpectedTraffic,
) -> Test {
    Test {
        proto,
        client_updates: None,
        server_updates: Some(vec![fnetfilter::Rule {
            action: fnetfilter::Action::Drop,
            direction: fnetfilter::Direction::Outgoing,
            proto,
            src_subnet: src_subnet.map(Box::new),
            src_subnet_invert_match,
            dst_subnet: dst_subnet.map(Box::new),
            dst_subnet_invert_match,
            src_port_range: fnetfilter::PortRange {
                start: *src_port_range.start(),
                end: *src_port_range.end(),
            },
            dst_port_range: fnetfilter::PortRange {
                start: *dst_port_range.start(),
                end: *dst_port_range.end(),
            },
            nic: 0,
            log: false,
            keep_state: false,
        }]),
        expected_traffic,
    }
}

/// Tests if the filter on client drops the incoming traffic to client.
pub fn client_incoming_drop_test(
    proto: fnetfilter::SocketProtocol,
    src_subnet: Option<fnet::Subnet>,
    src_subnet_invert_match: bool,
    dst_subnet: Option<fnet::Subnet>,
    dst_subnet_invert_match: bool,
    src_port_range: RangeInclusive<u16>,
    dst_port_range: RangeInclusive<u16>,
    expected_traffic: ExpectedTraffic,
) -> Test {
    Test {
        proto,
        client_updates: Some(vec![fnetfilter::Rule {
            action: fnetfilter::Action::Drop,
            direction: fnetfilter::Direction::Incoming,
            proto,
            src_subnet: src_subnet.map(Box::new),
            src_subnet_invert_match,
            dst_subnet: dst_subnet.map(Box::new),
            dst_subnet_invert_match,
            src_port_range: fnetfilter::PortRange {
                start: *src_port_range.start(),
                end: *src_port_range.end(),
            },
            dst_port_range: fnetfilter::PortRange {
                start: *dst_port_range.start(),
                end: *dst_port_range.end(),
            },
            nic: 0,
            log: false,
            keep_state: false,
        }]),
        server_updates: None,
        expected_traffic,
    }
}

pub fn subnet_with_offset(
    fnet::Subnet { mut addr, prefix_len }: fnet::Subnet,
    offset: u8,
) -> fnet::Subnet {
    let last_mut = match addr {
        fnet::IpAddress::Ipv4(fnet::Ipv4Address { ref mut addr }) => addr.last_mut(),
        fnet::IpAddress::Ipv6(fnet::Ipv6Address { ref mut addr }) => addr.last_mut(),
    };
    *last_mut.expect("should have at least 1 byte in addresses") += offset;

    fnet::Subnet { addr, prefix_len }
}
