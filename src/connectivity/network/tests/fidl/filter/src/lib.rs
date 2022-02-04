// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use std::{convert::TryFrom as _, ops::RangeInclusive};

use anyhow::Context as _;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_ext as fnet_ext;
use fidl_fuchsia_net_filter as fnetfilter;
use fidl_fuchsia_net_interfaces_ext as fnet_interfaces_ext;
use fidl_fuchsia_net_stack as fnet_stack;
use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use fuchsia_async::{DurationExt as _, TimeoutExt as _};
use futures::{
    io::AsyncReadExt as _, io::AsyncWriteExt as _, FutureExt as _, StreamExt, TryFutureExt as _,
};
use net_declare::{fidl_mac, fidl_subnet};
use netemul::{RealmTcpListener as _, RealmTcpStream as _, RealmUdpSocket as _};
use netfilter::FidlReturn as _;
use netstack_testing_common::realms::{Netstack2, TestSandboxExt as _};
use netstack_testing_common::{
    ping as ping_helper, ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT, ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
};
use netstack_testing_macros::variants_test;
use test_case::test_case;

const CLIENT_IPV4_SUBNET: fnet::Subnet = fidl_subnet!("192.168.0.2/24");
const SERVER_IPV4_SUBNET: fnet::Subnet = fidl_subnet!("192.168.0.1/24");
const CLIENT_MAC_ADDRESS: fnet::MacAddress = fidl_mac!("02:00:00:00:00:01");
const SERVER_MAC_ADDRESS: fnet::MacAddress = fidl_mac!("02:00:00:00:00:02");

const CLIENT_PORT: u16 = 1234;
const SERVER_PORT: u16 = 8080;

const CLIENT_PAYLOAD: &'static str = "Enjoy your food!";
const SERVER_PAYLOAD: &'static str = "Thanks, you too...";

#[derive(Copy, Clone)]
enum ExpectedTraffic {
    ClientToServerOnly,
    TwoWay,
}

struct Test {
    proto: fnetfilter::SocketProtocol,
    client_updates: Option<Vec<fnetfilter::Rule>>,
    server_updates: Option<Vec<fnetfilter::Rule>>,
    expected_traffic: ExpectedTraffic,
}

async fn run_udp_socket_test(
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

async fn run_tcp_socket_test(
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

async fn run_socket_test(
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

async fn test_filter<E: netemul::Endpoint>(name: &str, test: Test) {
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
            &netemul::InterfaceConfig::StaticIp(CLIENT_IPV4_SUBNET),
        )
        .await
        .expect("client failed to join network");
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
            &netemul::InterfaceConfig::StaticIp(SERVER_IPV4_SUBNET),
        )
        .await
        .expect("server failed to join network");

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

    // Initial sanity check (no filters set).
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
fn server_outgoing_drop_test(
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
fn client_incoming_drop_test(
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

#[variants_test]
async fn drop_udp_incoming<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        client_incoming_drop_test(
            fnetfilter::SocketProtocol::Udp,
            None,                                /* src_subnet */
            false,                               /* src_subnet_invert_match */
            None,                                /* dst_subnet */
            false,                               /* dst_subnet_invert_match */
            0..=0,                               /* src_port_range */
            0..=0,                               /* dst_port_range */
            ExpectedTraffic::ClientToServerOnly, /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_tcp_incoming<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        client_incoming_drop_test(
            fnetfilter::SocketProtocol::Tcp,
            None,                                /* src_subnet */
            false,                               /* src_subnet_invert_match */
            None,                                /* dst_subnet */
            false,                               /* dst_subnet_invert_match */
            0..=0,                               /* src_port_range */
            0..=0,                               /* dst_port_range */
            ExpectedTraffic::ClientToServerOnly, /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_udp_outgoing<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        server_outgoing_drop_test(
            fnetfilter::SocketProtocol::Udp,
            None,                                /* src_subnet */
            false,                               /* src_subnet_invert_match */
            None,                                /* dst_subnet */
            false,                               /* dst_subnet_invert_match */
            0..=0,                               /* src_port_range */
            0..=0,                               /* dst_port_range */
            ExpectedTraffic::ClientToServerOnly, /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_tcp_outgoing<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        server_outgoing_drop_test(
            fnetfilter::SocketProtocol::Tcp,
            None,                                /* src_subnet */
            false,                               /* src_subnet_invert_match */
            None,                                /* dst_subnet */
            false,                               /* dst_subnet_invert_match */
            0..=0,                               /* src_port_range */
            0..=0,                               /* dst_port_range */
            ExpectedTraffic::ClientToServerOnly, /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_udp_incoming_within_port_range<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        client_incoming_drop_test(
            fnetfilter::SocketProtocol::Udp,
            None,                                /* src_subnet */
            false,                               /* src_subnet_invert_match */
            None,                                /* dst_subnet */
            false,                               /* dst_subnet_invert_match */
            SERVER_PORT - 1..=SERVER_PORT + 1,   /* src_port_range */
            CLIENT_PORT..=CLIENT_PORT,           /* dst_port_range */
            ExpectedTraffic::ClientToServerOnly, /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_tcp_incoming_within_port_range<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        client_incoming_drop_test(
            fnetfilter::SocketProtocol::Tcp,
            None,                                /* src_subnet */
            false,                               /* src_subnet_invert_match */
            None,                                /* dst_subnet */
            false,                               /* dst_subnet_invert_match */
            SERVER_PORT - 1..=SERVER_PORT + 1,   /* src_port_range */
            0..=0,                               /* dst_port_range */
            ExpectedTraffic::ClientToServerOnly, /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_udp_outgoing_within_port_range<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        server_outgoing_drop_test(
            fnetfilter::SocketProtocol::Udp,
            None,                                /* src_subnet */
            false,                               /* src_subnet_invert_match */
            None,                                /* dst_subnet */
            false,                               /* dst_subnet_invert_match */
            SERVER_PORT - 1..=SERVER_PORT + 1,   /* src_port_range */
            CLIENT_PORT..=CLIENT_PORT,           /* dst_port_range */
            ExpectedTraffic::ClientToServerOnly, /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_tcp_outgoing_within_port_range<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        server_outgoing_drop_test(
            fnetfilter::SocketProtocol::Tcp,
            None,                                /* src_subnet */
            false,                               /* src_subnet_invert_match */
            None,                                /* dst_subnet */
            false,                               /* dst_subnet_invert_match */
            SERVER_PORT - 1..=SERVER_PORT + 1,   /* src_port_range */
            0..=0,                               /* dst_port_range */
            ExpectedTraffic::ClientToServerOnly, /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_udp_incoming_outside_port_range<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        client_incoming_drop_test(
            fnetfilter::SocketProtocol::Udp,
            None,                              /* src_subnet */
            false,                             /* src_subnet_invert_match */
            None,                              /* dst_subnet */
            false,                             /* dst_subnet_invert_match */
            SERVER_PORT + 1..=SERVER_PORT + 3, /* src_port_range */
            CLIENT_PORT..=CLIENT_PORT,         /* dst_port_range */
            ExpectedTraffic::TwoWay,           /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_tcp_incoming_outside_port_range<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        client_incoming_drop_test(
            fnetfilter::SocketProtocol::Tcp,
            None,                              /* src_subnet */
            false,                             /* src_subnet_invert_match */
            None,                              /* dst_subnet */
            false,                             /* dst_subnet_invert_match */
            SERVER_PORT + 1..=SERVER_PORT + 3, /* src_port_range */
            CLIENT_PORT..=CLIENT_PORT,         /* dst_port_range */
            ExpectedTraffic::TwoWay,           /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_udp_outgoing_outside_port_range<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        server_outgoing_drop_test(
            fnetfilter::SocketProtocol::Udp,
            None,                              /* src_subnet */
            false,                             /* src_subnet_invert_match */
            None,                              /* dst_subnet */
            false,                             /* dst_subnet_invert_match */
            SERVER_PORT + 1..=SERVER_PORT + 3, /* src_port_range */
            CLIENT_PORT..=CLIENT_PORT,         /* dst_port_range */
            ExpectedTraffic::TwoWay,           /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_tcp_outgoing_outside_port_range<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        server_outgoing_drop_test(
            fnetfilter::SocketProtocol::Tcp,
            None,                              /* src_subnet */
            false,                             /* src_subnet_invert_match */
            None,                              /* dst_subnet */
            false,                             /* dst_subnet_invert_match */
            SERVER_PORT + 1..=SERVER_PORT + 3, /* src_port_range */
            CLIENT_PORT..=CLIENT_PORT,         /* dst_port_range */
            ExpectedTraffic::TwoWay,           /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_udp_incoming_with_address_range<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        client_incoming_drop_test(
            fnetfilter::SocketProtocol::Udp,
            Some(SERVER_IPV4_SUBNET),            /* src_subnet */
            false,                               /* src_subnet_invert_match */
            Some(CLIENT_IPV4_SUBNET),            /* dst_subnet */
            false,                               /* dst_subnet_invert_match */
            0..=0,                               /* src_port_range */
            0..=0,                               /* dst_port_range */
            ExpectedTraffic::ClientToServerOnly, /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_tcp_incoming_with_address_range<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        client_incoming_drop_test(
            fnetfilter::SocketProtocol::Tcp,
            Some(SERVER_IPV4_SUBNET),            /* src_subnet */
            false,                               /* src_subnet_invert_match */
            Some(CLIENT_IPV4_SUBNET),            /* dst_subnet */
            false,                               /* dst_subnet_invert_match */
            0..=0,                               /* src_port_range */
            0..=0,                               /* dst_port_range */
            ExpectedTraffic::ClientToServerOnly, /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_udp_outgoing_with_address_range<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        server_outgoing_drop_test(
            fnetfilter::SocketProtocol::Udp,
            Some(SERVER_IPV4_SUBNET),            /* src_subnet */
            false,                               /* src_subnet_invert_match */
            Some(CLIENT_IPV4_SUBNET),            /* dst_subnet */
            false,                               /* dst_subnet_invert_match */
            0..=0,                               /* src_port_range */
            0..=0,                               /* dst_port_range */
            ExpectedTraffic::ClientToServerOnly, /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_tcp_outgoing_with_address_range<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        server_outgoing_drop_test(
            fnetfilter::SocketProtocol::Tcp,
            Some(SERVER_IPV4_SUBNET),            /* src_subnet */
            false,                               /* src_subnet_invert_match */
            Some(CLIENT_IPV4_SUBNET),            /* dst_subnet */
            false,                               /* dst_subnet_invert_match */
            0..=0,                               /* src_port_range */
            0..=0,                               /* dst_port_range */
            ExpectedTraffic::ClientToServerOnly, /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_udp_incoming_with_src_address_invert<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        client_incoming_drop_test(
            fnetfilter::SocketProtocol::Udp,
            Some(SERVER_IPV4_SUBNET), /* src_subnet */
            true,                     /* src_subnet_invert_match */
            Some(CLIENT_IPV4_SUBNET), /* dst_subnet */
            false,                    /* dst_subnet_invert_match */
            0..=0,                    /* src_port_range */
            0..=0,                    /* dst_port_range */
            ExpectedTraffic::TwoWay,  /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_tcp_incoming_with_src_address_invert<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        client_incoming_drop_test(
            fnetfilter::SocketProtocol::Tcp,
            Some(SERVER_IPV4_SUBNET), /* src_subnet */
            true,                     /* src_subnet_invert_match */
            Some(CLIENT_IPV4_SUBNET), /* dst_subnet */
            false,                    /* dst_subnet_invert_match */
            0..=0,                    /* src_port_range */
            0..=0,                    /* dst_port_range */
            ExpectedTraffic::TwoWay,  /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_udp_outgoing_with_src_address_invert<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        server_outgoing_drop_test(
            fnetfilter::SocketProtocol::Udp,
            Some(SERVER_IPV4_SUBNET), /* src_subnet */
            true,                     /* src_subnet_invert_match */
            Some(CLIENT_IPV4_SUBNET), /* dst_subnet */
            false,                    /* dst_subnet_invert_match */
            0..=0,                    /* src_port_range */
            0..=0,                    /* dst_port_range */
            ExpectedTraffic::TwoWay,  /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_tcp_outgoing_with_src_address_invert<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        server_outgoing_drop_test(
            fnetfilter::SocketProtocol::Tcp,
            Some(SERVER_IPV4_SUBNET), /* src_subnet */
            true,                     /* src_subnet_invert_match */
            Some(CLIENT_IPV4_SUBNET), /* dst_subnet */
            false,                    /* dst_subnet_invert_match */
            0..=0,                    /* src_port_range */
            0..=0,                    /* dst_port_range */
            ExpectedTraffic::TwoWay,  /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_udp_incoming_with_dst_address_invert<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        client_incoming_drop_test(
            fnetfilter::SocketProtocol::Udp,
            Some(SERVER_IPV4_SUBNET), /* src_subnet */
            false,                    /* src_subnet_invert_match */
            Some(CLIENT_IPV4_SUBNET), /* dst_subnet */
            true,                     /* dst_subnet_invert_match */
            0..=0,                    /* src_port_range */
            0..=0,                    /* dst_port_range */
            ExpectedTraffic::TwoWay,  /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_tcp_incoming_with_dst_address_invert<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        client_incoming_drop_test(
            fnetfilter::SocketProtocol::Tcp,
            Some(SERVER_IPV4_SUBNET), /* src_subnet */
            false,                    /* src_subnet_invert_match */
            Some(CLIENT_IPV4_SUBNET), /* dst_subnet */
            true,                     /* dst_subnet_invert_match */
            0..=0,                    /* src_port_range */
            0..=0,                    /* dst_port_range */
            ExpectedTraffic::TwoWay,  /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_udp_outgoing_with_dst_address_invert<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        server_outgoing_drop_test(
            fnetfilter::SocketProtocol::Udp,
            Some(SERVER_IPV4_SUBNET), /* src_subnet */
            false,                    /* src_subnet_invert_match */
            Some(CLIENT_IPV4_SUBNET), /* dst_subnet */
            true,                     /* dst_subnet_invert_match */
            0..=0,                    /* src_port_range */
            0..=0,                    /* dst_port_range */
            ExpectedTraffic::TwoWay,  /* expected_traffic */
        ),
    )
    .await
}

#[variants_test]
async fn drop_tcp_outgoing_with_dst_address_invert<E: netemul::Endpoint>(name: &str) {
    test_filter::<E>(
        name,
        server_outgoing_drop_test(
            fnetfilter::SocketProtocol::Tcp,
            Some(SERVER_IPV4_SUBNET), /* src_subnet */
            false,                    /* src_subnet_invert_match */
            Some(CLIENT_IPV4_SUBNET), /* dst_subnet */
            true,                     /* dst_subnet_invert_match */
            0..=0,                    /* src_port_range */
            0..=0,                    /* dst_port_range */
            ExpectedTraffic::TwoWay,  /* expected_traffic */
        ),
    )
    .await
}

fn subnet_with_offset(
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

enum NatNic {
    RouterNic1,
    RouterNic2,
}

struct NatTestCase {
    src_subnet: fnet::Subnet,
    dst_subnet: fnet::Subnet,

    nat_proto: fnetfilter::SocketProtocol,
    nat_src_subnet: fnet::Subnet,
    nat_outgoing_nic: NatNic,
    cycle_dst_net: bool,
    expect_nat: bool,
}

struct HostNetwork<'a> {
    net: netemul::TestNetwork<'a>,
    router_ep: netemul::TestInterface<'a>,
    router_addr: fnet::Subnet,

    host_realm: netemul::TestRealm<'a>,
    host_ep: netemul::TestInterface<'a>,
    host_addr: fnet::Subnet,
}

struct MasqueradeNatNetwork<'a> {
    router_realm: netemul::TestRealm<'a>,

    net1: HostNetwork<'a>,
    net2: HostNetwork<'a>,
}

fn subnet_to_addr(fnet::Subnet { addr, prefix_len: _ }: fnet::Subnet) -> std::net::IpAddr {
    let fnet_ext::IpAddress(addr) = fnet_ext::IpAddress::from(addr);
    addr
}

async fn setup_masquerate_nat_network<'a, E: netemul::Endpoint>(
    sandbox: &'a netemul::TestSandbox,
    name: &str,
    test_case: &NatTestCase,
) -> MasqueradeNatNetwork<'a> {
    let NatTestCase {
        src_subnet,
        dst_subnet,
        nat_proto,
        nat_src_subnet,
        nat_outgoing_nic,
        cycle_dst_net,
        expect_nat: _,
    } = test_case;

    let router_realm = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_router", name))
        .expect("failed to create router_realm");

    let stack = router_realm
        .connect_to_protocol::<fnet_stack::StackMarker>()
        .expect("failed to connect to netstack");
    let () = stack.enable_ip_forwarding().await.expect("failed to enable ip forwarding");

    async fn configure_host_network<'a, E: netemul::Endpoint>(
        sandbox: &'a netemul::TestSandbox,
        name: &str,
        router_realm: &netemul::TestRealm<'a>,
        router_if_name: Option<String>,
        net_num: u8,
        subnet: fnet::Subnet,
        other_subnet: fnet::Subnet,
    ) -> HostNetwork<'a> {
        let router_addr = subnet_with_offset(subnet, 1);
        let host_addr = subnet_with_offset(subnet, 2);

        let net = sandbox
            .create_network(format!("net{}", net_num))
            .await
            .expect("failed to create network");

        let host_realm = sandbox
            .create_netstack_realm::<Netstack2, _>(format!("{}_host{}", name, net_num))
            .expect("failed to create host realm");

        let router_ep = router_realm
            .join_network_with_if_name::<E, _>(
                &net,
                format!("router_ep{}", net_num),
                &netemul::InterfaceConfig::StaticIp(router_addr),
                router_if_name,
            )
            .await
            .expect("router failed to join network");

        let host_ep = host_realm
            .join_network::<E, _>(
                &net,
                format!("host{}_ep", net_num),
                &netemul::InterfaceConfig::StaticIp(host_addr),
            )
            .await
            .expect("host failed to join network");

        let stack = host_realm
            .connect_to_protocol::<fnet_stack::StackMarker>()
            .expect("failed to connect to netstack");
        let fnet::Subnet { addr: next_hop, prefix_len: _ } = router_addr;
        let () = stack
            .add_forwarding_entry(&mut fnet_stack::ForwardingEntry {
                subnet: fnet_ext::apply_subnet_mask(other_subnet),
                device_id: 0,
                next_hop: Some(Box::new(next_hop)),
                metric: 0,
            })
            .map(move |r| {
                r.squash_result().unwrap_or_else(|e| {
                    panic!("failed to add route to other subnet {:?}: {}", other_subnet, e)
                })
            })
            .await;

        HostNetwork { net, router_ep, router_addr, host_realm, host_ep, host_addr }
    }

    let net1 = configure_host_network::<E>(
        &sandbox,
        name,
        &router_realm,
        None,
        1,
        *src_subnet,
        *dst_subnet,
    )
    .await;
    let HostNetwork {
        net: _,
        router_ep: router_ep1,
        router_addr: _,
        host_realm: _,
        host_ep: _,
        host_addr: _,
    } = &net1;

    let net2_factory = |router_if_name| async {
        configure_host_network::<E>(
            &sandbox,
            name,
            &router_realm,
            router_if_name,
            2,
            *dst_subnet,
            *src_subnet,
        )
        .await
    };

    let mut net2 = net2_factory(None).await;
    let HostNetwork {
        net: _,
        router_ep: router_ep2,
        router_addr: _,
        host_realm: _,
        host_ep: _,
        host_addr: _,
    } = &net2;

    let mut updates = vec![fnetfilter::Nat {
        proto: *nat_proto,
        src_subnet: *nat_src_subnet,
        outgoing_nic: u32::try_from(match nat_outgoing_nic {
            NatNic::RouterNic1 => router_ep1.id(),
            NatNic::RouterNic2 => router_ep2.id(),
        })
        .expect("NIC ID should fit in a u32"),
    }];

    let router_filter = router_realm
        .connect_to_protocol::<fnetfilter::FilterMarker>()
        .expect("failed to connect to filter service");

    let () = router_filter
        .enable_interface(router_ep2.id())
        .await
        .transform_result()
        .expect("error enabling filter on router_ep2");
    let (rules, generation) =
        router_filter.get_nat_rules().await.transform_result().expect("failed to get NAT rules");
    assert_eq!(&rules, &[]);
    let () = router_filter
        .update_nat_rules(&mut updates.iter_mut(), generation)
        .await
        .transform_result()
        .expect("failed to update NAT rules");
    let generation = generation + 1;
    {
        let (got_nat_rules, got_generation) = router_filter
            .get_nat_rules()
            .await
            .transform_result()
            .expect("failed to get NAT rules");
        assert_eq!(got_nat_rules, updates);
        assert_eq!(got_generation, generation);
    }

    if *cycle_dst_net {
        let router_ep2_id = router_ep2.id();
        let state_stream = fnet_interfaces_ext::event_stream(
            router_ep2.get_interfaces_watcher().expect("error getting interfaces watcher"),
        );
        futures::pin_mut!(state_stream);

        // Make sure the interfaces watcher stream knows about router_ep2's existence
        // so we can reliably observe its removal later.
        let mut router_ep2_interface_state = fnet_interfaces_ext::existing(
            &mut state_stream,
            fnet_interfaces_ext::InterfaceState::Unknown(router_ep2_id),
        )
        .await
        .expect("error reading existing interface event");

        let router_ep2_name = match &router_ep2_interface_state {
            fnet_interfaces_ext::InterfaceState::Known(fnet_interfaces_ext::Properties {
                id,
                name,
                device_class: _,
                online: _,
                addresses: _,
                has_default_ipv4_route: _,
                has_default_ipv6_route: _,
            }) => {
                assert_eq!(*id, router_ep2_id);
                name.clone()
            }
            fnet_interfaces_ext::InterfaceState::Unknown(id) => {
                panic!("expected known interface state for router_ep2(id={}); got unknown state for ID = {}",
                       router_ep2_id, id)
            }
        };

        let () = std::mem::drop(net2);
        let () = fnet_interfaces_ext::wait_interface_with_id(
            state_stream,
            &mut router_ep2_interface_state,
            |fnet_interfaces_ext::Properties {
                 id,
                 name: _,
                 device_class: _,
                 online: _,
                 addresses: _,
                 has_default_ipv4_route: _,
                 has_default_ipv6_route: _,
             }| {
                assert_eq!(*id, router_ep2_id);
                None
            },
        )
        .await
        .map_or_else(
            |err| match err {
                fnet_interfaces_ext::WatcherOperationError::Update(
                    fnet_interfaces_ext::UpdateError::Removed,
                ) => {}
                err => panic!("unexpected error waiting for interface removal: {:?}", err),
            },
            |_: ()| panic!("expected to get removed event"),
        );

        // The NAT rule for a NIC should be removed when the NIC is removed.
        let (got_nat_rules, got_generation) = router_filter
            .get_nat_rules()
            .await
            .transform_result()
            .expect("failed to get NAT rules");
        assert_eq!(got_nat_rules, []);
        assert_eq!(got_generation, generation + 1);

        net2 = net2_factory(Some(router_ep2_name)).await;
        let HostNetwork {
            net: _,
            router_ep: router_ep2,
            router_addr: _,
            host_realm: _,
            host_ep: _,
            host_addr: _,
        } = &net2;
        assert_ne!(router_ep2_id, router_ep2.id());
    }

    MasqueradeNatNetwork { router_realm, net1, net2 }
}

const IPV4_SUBNET1: fnet::Subnet = fidl_subnet!("10.0.0.0/24");
const IPV4_SUBNET2: fnet::Subnet = fidl_subnet!("192.168.0.0/24");
const IPV6_SUBNET1: fnet::Subnet = fidl_subnet!("a::/24");
const IPV6_SUBNET2: fnet::Subnet = fidl_subnet!("b::/24");

#[variants_test]
#[test_case(
    "perform_nat44",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
        expect_nat: true,
    }; "perform_nat44")]
#[test_case(
    "dont_perform_nat44_outgoing_nic_cycled",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: true,
        expect_nat: false,
    }; "dont_perform_nat44_outgoing_nic_cycled")]
#[test_case(
    "dont_perform_nat44_different_protocol",
    NatTestCase {
        src_subnet: IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
        expect_nat: false,
    }; "dont_perform_nat44_different_protocol")]
#[test_case(
    "dont_perform_nat44_different_src_subnet",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV4_SUBNET2,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
        expect_nat: false,
    }; "dont_perform_nat44_different_src_subnet")]
#[test_case(
    "dont_perform_nat44_different_nic",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic1,
        cycle_dst_net: false,
        expect_nat: true,
    }; "dont_perform_nat44_different_nic")]
#[test_case(
    "perform_nat66",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
        expect_nat: true,
    }; "perform_nat66")]
#[test_case(
    "dont_perform_nat66_outgoing_nic_cycled",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: true,
        expect_nat: false,
    }; "dont_perform_nat66_outgoing_nic_cycled")]
#[test_case(
    "dont_perform_nat66_different_protocol",
    NatTestCase {
        src_subnet: IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
        expect_nat: false,
    }; "dont_perform_nat66_different_protocol")]
#[test_case(
    "dont_perform_nat66_different_src_subnet",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV6_SUBNET2,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
        expect_nat: false,
    }; "dont_perform_nat66_different_src_subnet")]
#[test_case(
    "dont_perform_nat66_different_nic",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic1,
        cycle_dst_net: false,
        expect_nat: true,
    }; "dont_perform_nat66_different_nic")]
async fn masquerade_nat_udp<E: netemul::Endpoint>(
    test_name: &str,
    sub_test_name: &str,
    test_case: NatTestCase,
) {
    let name = format!("{}_{}", test_name, sub_test_name);
    let name = name.as_str();

    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");

    let MasqueradeNatNetwork {
        router_realm: _router_realm,
        net1:
            HostNetwork {
                net: _net1,
                router_ep: _router_ep1,
                router_addr: _,
                host_realm: host1_realm,
                host_ep: _host1_ep,
                host_addr: host1_addr,
            },
        net2:
            HostNetwork {
                net: _net2,
                router_ep: _router_ep2,
                router_addr: router_ep2_addr,
                host_realm: host2_realm,
                host_ep: _host2_ep,
                host_addr: host2_addr,
            },
    } = setup_masquerate_nat_network::<E>(&sandbox, name, &test_case).await;

    let get_sock = |realm, subnet| async move {
        let addr = subnet_to_addr(subnet);
        let sock =
            fuchsia_async::net::UdpSocket::bind_in_realm(realm, std::net::SocketAddr::new(addr, 0))
                .await
                .expect("failed to create socket");
        let addr = sock.local_addr().expect("failed to get socket's local addr");

        (sock, addr)
    };

    let (host1_sock, host1_sockaddr) = get_sock(&host1_realm, host1_addr).await;
    let (host2_sock, host2_sockaddr) = get_sock(&host2_realm, host2_addr).await;

    // Send a packet from host1 to host2.
    const SEND_SIZE: usize = 4;
    const SEND_BUF: [u8; SEND_SIZE] = [1, 2, 4, 5];
    assert_eq!(
        host1_sock
            .send_to(&SEND_BUF, host2_sockaddr)
            .await
            .expect("failed to send from host1 to host2"),
        SEND_BUF.len()
    );

    // Host2 should loop the packet back to host1.
    {
        let mut recv_buf = [0; SEND_SIZE + 1];
        let (got_byte_count, sender) =
            host2_sock.recv_from(&mut recv_buf).await.expect("failed to recv from host2_sock");
        assert_eq!(got_byte_count, SEND_BUF.len());
        let recv_buf = &recv_buf[..got_byte_count];
        assert_eq!(recv_buf, &SEND_BUF);
        let NatTestCase {
            src_subnet: _,
            dst_subnet: _,
            nat_proto: _,
            nat_src_subnet: _,
            nat_outgoing_nic: _,
            cycle_dst_net: _,
            expect_nat,
        } = test_case;

        let expected_sender = if expect_nat {
            std::net::SocketAddr::new(subnet_to_addr(router_ep2_addr), host1_sockaddr.port())
        } else {
            host1_sockaddr
        };
        assert_eq!(sender, expected_sender);
        assert_eq!(
            host2_sock.send_to(recv_buf, sender).await.expect("failed to send from host1 to host2"),
            SEND_BUF.len()
        );
    };

    // Make sure the packet was looped back to host1 by host2.
    {
        let mut recv_buf = [0; SEND_SIZE + 1];
        let (got_byte_count, sender) =
            host1_sock.recv_from(&mut recv_buf).await.expect("failed to recv from host2_sock");
        assert_eq!(got_byte_count, SEND_BUF.len());
        assert_eq!(&recv_buf[..SEND_BUF.len()], &SEND_BUF);
        assert_eq!(sender, host2_sockaddr);
    }
}

#[variants_test]
#[test_case(
    "perform_nat44",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
        expect_nat: true,
    }; "perform_nat44")]
#[test_case(
    "dont_perform_nat44_outgoing_nic_cycled",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: true,
        expect_nat: false,
    }; "dont_perform_nat44_outgoing_nic_cycled")]
#[test_case(
    "dont_perform_nat44_different_protocol",
    NatTestCase {
        src_subnet: IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
        expect_nat: false,
    }; "dont_perform_nat44_different_protocol")]
#[test_case(
    "dont_perform_nat44_different_src_subnet",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV4_SUBNET2,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
        expect_nat: false,
    }; "dont_perform_nat44_different_src_subnet")]
#[test_case(
    "dont_perform_nat44_different_nic",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic1,
        cycle_dst_net: false,
        expect_nat: true,
    }; "dont_perform_nat44_different_nic")]
#[test_case(
    "perform_nat66",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
        expect_nat: true,
    }; "perform_nat66")]
#[test_case(
    "dont_perform_nat66_outgoing_nic_cycled",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: true,
        expect_nat: false,
    }; "dont_perform_nat66_outgoing_nic_cycled")]
#[test_case(
    "dont_perform_nat66_different_protocol",
    NatTestCase {
        src_subnet: IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Udp,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
        expect_nat: false,
    }; "dont_perform_nat66_different_protocol")]
#[test_case(
    "dont_perform_nat66_different_src_subnet",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV6_SUBNET2,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
        expect_nat: false,
    }; "dont_perform_nat66_different_src_subnet")]
#[test_case(
    "dont_perform_nat66_different_nic",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic1,
        cycle_dst_net: false,
        expect_nat: true,
    }; "dont_perform_nat66_different_nic")]
async fn masquerade_nat_tcp<E: netemul::Endpoint>(
    test_name: &str,
    sub_test_name: &str,
    test_case: NatTestCase,
) {
    let name = format!("{}_{}", test_name, sub_test_name);
    let name = name.as_str();

    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");

    let MasqueradeNatNetwork {
        router_realm: _router_realm,
        net1:
            HostNetwork {
                net: _net1,
                router_ep: _router_ep1,
                router_addr: _,
                host_realm: host1_realm,
                host_ep: _host1_ep,
                host_addr: _,
            },
        net2:
            HostNetwork {
                net: _net2,
                router_ep: _router_ep2,
                router_addr: router_ep2_addr,
                host_realm: host2_realm,
                host_ep: _host2_ep,
                host_addr: host2_addr,
            },
    } = setup_masquerate_nat_network::<E>(&sandbox, name, &test_case).await;

    let host2_listener = fuchsia_async::net::TcpListener::listen_in_realm(
        &host2_realm,
        std::net::SocketAddr::new(subnet_to_addr(host2_addr), 0),
    )
    .await
    .expect("failed to create TCP listener");
    let host2_listener_addr =
        host2_listener.local_addr().expect("failed to get host2_listener's local addr");

    let host1_client =
        fuchsia_async::net::TcpStream::connect_in_realm(&host1_realm, host2_listener_addr)
            .await
            .expect("failed to connect to host2 from host1");
    let (_host2_listener, _accepted_sock, client_addr) =
        host2_listener.accept().await.expect("failed to accept connection");

    let host1_client_addr =
        host1_client.std().local_addr().expect("failed to get host1_client's local addr");
    let NatTestCase {
        src_subnet: _,
        dst_subnet: _,
        nat_proto: _,
        nat_src_subnet: _,
        nat_outgoing_nic: _,
        cycle_dst_net: _,
        expect_nat,
    } = test_case;

    let expected_client_addr = if expect_nat {
        std::net::SocketAddr::new(subnet_to_addr(router_ep2_addr), host1_client_addr.port())
    } else {
        host1_client_addr
    };
    assert_eq!(client_addr, expected_client_addr);
}

#[variants_test]
#[test_case(
    "perform_nat44",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Icmp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
        expect_nat: true,
    }; "perform_nat44")]
#[test_case(
    "dont_perform_nat44_outgoing_nic_cycled",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Icmp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: true,
        expect_nat: false,
    }; "dont_perform_nat44_outgoing_nic_cycled")]
#[test_case(
    "dont_perform_nat44_different_protocol",
    NatTestCase {
        src_subnet: IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
        expect_nat: false,
    }; "dont_perform_nat44_different_protocol")]
#[test_case(
    "dont_perform_nat44_different_src_subnet",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Icmp,
        nat_src_subnet: IPV4_SUBNET2,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
        expect_nat: false,
    }; "dont_perform_nat44_different_src_subnet")]
#[test_case(
    "dont_perform_nat44_different_nic",
    NatTestCase {
        src_subnet:IPV4_SUBNET1,
        dst_subnet: IPV4_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Icmp,
        nat_src_subnet: IPV4_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic1,
        cycle_dst_net: false,
        expect_nat: true,
    }; "dont_perform_nat44_different_nic")]
#[test_case(
    "perform_nat66",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Icmpv6,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
        expect_nat: true,
    }; "perform_nat66")]
#[test_case(
    "dont_perform_nat66_outgoing_nic_cycled",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Icmpv6,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: true,
        expect_nat: false,
    }; "dont_perform_nat66_outgoing_nic_cycled")]
#[test_case(
    "dont_perform_nat66_different_protocol",
    NatTestCase {
        src_subnet: IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Tcp,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
        expect_nat: false,
    }; "dont_perform_nat66_different_protocol")]
#[test_case(
    "dont_perform_nat66_different_src_subnet",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Icmpv6,
        nat_src_subnet: IPV6_SUBNET2,
        nat_outgoing_nic: NatNic::RouterNic2,
        cycle_dst_net: false,
        expect_nat: false,
    }; "dont_perform_nat66_different_src_subnet")]
#[test_case(
    "dont_perform_nat66_different_nic",
    NatTestCase {
        src_subnet:IPV6_SUBNET1,
        dst_subnet: IPV6_SUBNET2,
        nat_proto: fnetfilter::SocketProtocol::Icmpv6,
        nat_src_subnet: IPV6_SUBNET1,
        nat_outgoing_nic: NatNic::RouterNic1,
        cycle_dst_net: false,
        expect_nat: true,
    }; "dont_perform_nat66_different_nic")]
async fn masquerade_nat_ping<E: netemul::Endpoint>(
    test_name: &str,
    sub_test_name: &str,
    test_case: NatTestCase,
) {
    let name = format!("{}_{}", test_name, sub_test_name);
    let name = name.as_str();

    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");

    let MasqueradeNatNetwork {
        router_realm: _router_realm,
        net1:
            HostNetwork {
                net: _net1,
                router_ep: _router_ep1,
                router_addr: _,
                host_realm: host1_realm,
                host_ep: host1_ep,
                host_addr: host1_addr,
            },
        net2:
            HostNetwork {
                net: _net2,
                router_ep: _router_ep2,
                router_addr: _router_ep2_addr,
                host_realm: host2_realm,
                host_ep: host2_ep,
                host_addr: host2_addr,
            },
    } = setup_masquerate_nat_network::<E>(&sandbox, name, &test_case).await;

    let ping_node = |realm, id, fnet::Subnet { addr, prefix_len: _ }| {
        let (v4_addrs, v6_addrs) = match addr {
            fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr }) => (vec![addr.into()], vec![]),
            fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr }) => (vec![], vec![addr.into()]),
        };
        ping_helper::Node::new(realm, id, v4_addrs, v6_addrs)
    };

    let host1_node = ping_node(&host1_realm, host1_ep.id(), host1_addr);
    let host2_node = ping_node(&host2_realm, host2_ep.id(), host2_addr);
    let () = host1_node
        .ping_pairwise(&[host2_node])
        .await
        .expect("expected to successfully ping between host1 and host2");
}
