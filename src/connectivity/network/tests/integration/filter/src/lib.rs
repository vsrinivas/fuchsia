// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use std::ops::RangeInclusive;

use anyhow::Context as _;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_filter as fnetfilter;
use fuchsia_async::{DurationExt as _, TimeoutExt as _};
use futures::{io::AsyncReadExt as _, io::AsyncWriteExt as _, FutureExt as _, TryFutureExt as _};
use net_declare::fidl_subnet;
use netemul::{RealmTcpListener as _, RealmTcpStream as _, RealmUdpSocket as _};
use netstack_testing_common::realms::{Netstack2, TestSandboxExt as _};
use netstack_testing_common::{
    Result, ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT, ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
};
use netstack_testing_macros::variants_test;

const CLIENT_IPV4_SUBNET: fnet::Subnet = fidl_subnet!("192.168.0.2/24");
const SERVER_IPV4_SUBNET: fnet::Subnet = fidl_subnet!("192.168.0.1/24");

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
) -> Result {
    let fidl_fuchsia_net_ext::IpAddress(client_addr) =
        fidl_fuchsia_net_ext::IpAddress::from(client_addr);
    let client_addr = std::net::SocketAddr::new(client_addr, client_port);

    let fidl_fuchsia_net_ext::IpAddress(server_addr) =
        fidl_fuchsia_net_ext::IpAddress::from(server_addr);
    let server_addr = std::net::SocketAddr::new(server_addr, server_port);

    let client_sock = fuchsia_async::net::UdpSocket::bind_in_realm(client, client_addr)
        .await
        .context("failed to create client socket")?;
    let client_addr =
        client_sock.local_addr().context("failed to get local address from client socket")?;

    let server_sock = fuchsia_async::net::UdpSocket::bind_in_realm(server, server_addr)
        .await
        .context("failed to create server socket")?;

    let server_fut = async move {
        let mut buf = [0u8; 1024];
        let (r, from) = server_sock.recv_from(&mut buf[..]).await.context("recvfrom failed")?;
        assert_eq!(r, CLIENT_PAYLOAD.as_bytes().len());
        assert_eq!(&buf[..r], CLIENT_PAYLOAD.as_bytes());
        assert_eq!(from, client_addr);
        let r = server_sock
            .send_to(SERVER_PAYLOAD.as_bytes(), client_addr)
            .await
            .context("send to failed")?;
        assert_eq!(r, SERVER_PAYLOAD.as_bytes().len());
        Result::Ok(())
    };

    let client_fut = async move {
        let r = client_sock
            .send_to(CLIENT_PAYLOAD.as_bytes(), server_addr)
            .await
            .context("sendto failed")?;
        assert_eq!(r, CLIENT_PAYLOAD.as_bytes().len());

        let mut buf = [0u8; 1024];
        match expected_traffic {
            ExpectedTraffic::ClientToServerOnly => {
                match client_sock
                    .recv_from(&mut buf[..])
                    .map_ok(Some)
                    .on_timeout(ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT.after_now(), || Ok(None))
                    .await
                    .context("recvfrom failed")?
                {
                    Some((r, from)) => Result::Err(anyhow::anyhow!(
                        "unexpectedly received packet {:?} from {:?}",
                        &buf[..r],
                        from
                    )),
                    None => Result::Ok(()),
                }
            }
            ExpectedTraffic::TwoWay => {
                let (r, from) = client_sock
                    .recv_from(&mut buf[..])
                    .map(|r| r.context("recvfrom failed"))
                    .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
                        Err(anyhow::anyhow!(
                            "timed out waiting for packet from server after {:?}",
                            ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT
                        ))
                    })
                    .await?;
                assert_eq!(r, SERVER_PAYLOAD.as_bytes().len());
                assert_eq!(&buf[..r], SERVER_PAYLOAD.as_bytes());
                assert_eq!(from, server_addr);
                Result::Ok(())
            }
        }
    };

    let ((), ()) = futures::future::try_join(
        server_fut.map(|r| r.context("server-side error")),
        client_fut.map(|r| r.context("client-side error")),
    )
    .await?;
    Ok(())
}

async fn run_tcp_socket_test(
    server: &netemul::TestRealm<'_>,
    server_addr: fidl_fuchsia_net::IpAddress,
    server_port: u16,
    client: &netemul::TestRealm<'_>,
    client_addr: fidl_fuchsia_net::IpAddress,
    client_port: u16,
    expected_traffic: ExpectedTraffic,
) -> Result {
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
        .context("failed to create server socket")?;

    let server_fut = async move {
        match expected_traffic {
            ExpectedTraffic::ClientToServerOnly => {
                match listener
                    .accept()
                    .map_ok(Some)
                    .on_timeout(ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT.after_now(), || Ok(None))
                    .await
                    .context("failed to accept a connection")?
                {
                    Some(_) => Result::Err(anyhow::anyhow!("unexpectedly connected successfully")),
                    None => Result::Ok(()),
                }
            }
            ExpectedTraffic::TwoWay => {
                let (_listener, mut stream, from) = listener
                    .accept()
                    .map(|r| r.context("accept failed"))
                    .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
                        Err(anyhow::anyhow!(
                            "timed out waiting for a connection after {:?}",
                            ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT
                        ))
                    })
                    .await?;

                let mut buf = [0u8; 1024];
                let read_count =
                    stream.read(&mut buf).await.context("read from tcp server stream failed")?;

                assert_eq!(from.ip(), client_addr.ip());
                assert_eq!(read_count, CLIENT_PAYLOAD.as_bytes().len());
                assert_eq!(&buf[..read_count], CLIENT_PAYLOAD.as_bytes());

                let write_count = stream
                    .write(SERVER_PAYLOAD.as_bytes())
                    .await
                    .context("write to tcp server stream failed")?;
                assert_eq!(write_count, SERVER_PAYLOAD.as_bytes().len());
                Result::Ok(())
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
                    .context("failed to create client socket")?
                {
                    Some(_stream) => {
                        Result::Err(anyhow::anyhow!("unexpectedly connected successfully"))
                    }
                    None => Result::Ok(()),
                }
            }
            ExpectedTraffic::TwoWay => {
                let mut stream =
                    fuchsia_async::net::TcpStream::connect_in_realm(client, server_addr)
                        .map(|r| r.context("connect_in_realm failed"))
                        .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
                            Err(anyhow::anyhow!(
                                "timed out waiting for a connection after {:?}",
                                ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT
                            ))
                        })
                        .await?;

                let write_count = stream
                    .write(CLIENT_PAYLOAD.as_bytes())
                    .await
                    .context("write to tcp client stream failed")?;

                assert_eq!(write_count, CLIENT_PAYLOAD.as_bytes().len());

                let mut buf = [0u8; 1024];
                let read_count =
                    stream.read(&mut buf).await.context("read from tcp client stream failed")?;

                assert_eq!(read_count, SERVER_PAYLOAD.as_bytes().len());
                assert_eq!(&buf[..read_count], SERVER_PAYLOAD.as_bytes());
                Result::Ok(())
            }
        }
    };

    let ((), ()) = futures::future::try_join(
        client_fut.map(|r| r.context("client-side error")),
        server_fut.map(|r| r.context("server-side error")),
    )
    .await?;
    Ok(())
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
) -> Result {
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
        proto => Result::Err(anyhow::anyhow!("unexpected protocol {:?}", proto)),
    }
}

async fn test_filter<E: netemul::Endpoint>(name: &str, test: Test) -> Result {
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let net = sandbox.create_network("net").await.context("failed to create network")?;

    let client = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_client", name))
        .context("failed to create client realm")?;
    let client_ep = client
        .join_network::<E, _>(
            &net,
            "client",
            &netemul::InterfaceConfig::StaticIp(CLIENT_IPV4_SUBNET),
        )
        .await
        .context("client failed to join network")?;
    let client_filter = client
        .connect_to_protocol::<fnetfilter::FilterMarker>()
        .context("client failed to connect to filter service")?;

    let server = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_server", name))
        .context("failed to create server realm")?;
    let server_ep = server
        .join_network::<E, _>(
            &net,
            "server",
            &netemul::InterfaceConfig::StaticIp(SERVER_IPV4_SUBNET),
        )
        .await
        .context("server failed to join network")?;
    let server_filter = server
        .connect_to_protocol::<fnetfilter::FilterMarker>()
        .context("server failed to connect to filter service")?;

    let Test { proto, client_updates, server_updates, expected_traffic } = test;

    // Initial sanity check (no filters set).
    let status = client_filter
        .enable_interface(client_ep.id())
        .await
        .context("error enabling filter on client")?;
    assert_eq!(status, fnetfilter::Status::Ok);
    let status = server_filter
        .enable_interface(server_ep.id())
        .await
        .context("error enabling filter on server")?;
    assert_eq!(status, fnetfilter::Status::Ok);
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
    .await
    .context("error testing initial connection without filters")?;

    // Set the filters and do the test.
    let (_rules, mut server_generation, status) =
        server_filter.get_rules().await.context("failed to get server's filter rules")?;
    assert_eq!(status, fnetfilter::Status::Ok);
    let (_rules, mut client_generation, status) =
        client_filter.get_rules().await.context("failed to get client's filter rules")?;
    assert_eq!(status, fnetfilter::Status::Ok);
    if let Some(mut updates) = client_updates {
        let status = client_filter
            .update_rules(&mut updates.iter_mut(), client_generation)
            .await
            .context("failed to update client's filter rules")?;
        client_generation += 1;
        assert_eq!(status, fnetfilter::Status::Ok);
    }
    if let Some(mut updates) = server_updates {
        let status = server_filter
            .update_rules(&mut updates.iter_mut(), server_generation)
            .await
            .context("failed to update server's filter rules")?;
        server_generation += 1;
        assert_eq!(status, fnetfilter::Status::Ok);
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
    .await
    .context("error running socket test after updating filters")?;

    // Disable the filters on the interface and expect full connectivity.
    let status = client_filter
        .disable_interface(client_ep.id())
        .await
        .context("error disabling filter on client")?;
    assert_eq!(status, fnetfilter::Status::Ok);
    let status = server_filter
        .disable_interface(server_ep.id())
        .await
        .context("error disabling filter on server")?;
    assert_eq!(status, fnetfilter::Status::Ok);
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
    .await
    .context("error running socket test after disabling filters")?;

    // Reset and enable filters and expect full connectivity.
    let status = client_filter
        .enable_interface(client_ep.id())
        .await
        .context("error re-enabling filter on client")?;
    assert_eq!(status, fnetfilter::Status::Ok);
    let status = server_filter
        .enable_interface(server_ep.id())
        .await
        .context("error re-enabling filter on server")?;
    assert_eq!(status, fnetfilter::Status::Ok);
    let status = server_filter
        .update_rules(&mut Vec::new().iter_mut(), server_generation)
        .await
        .context("failed to reset client's filter rules")?;
    assert_eq!(status, fnetfilter::Status::Ok);
    let status = client_filter
        .update_rules(&mut Vec::new().iter_mut(), client_generation)
        .await
        .context("failed to reset client's filter rules")?;
    assert_eq!(status, fnetfilter::Status::Ok);
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
    .context("error running socket test after resetting filters")
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
async fn test_drop_udp_incoming<E: netemul::Endpoint>(name: &str) -> Result {
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
async fn test_drop_tcp_incoming<E: netemul::Endpoint>(name: &str) -> Result {
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
async fn test_drop_udp_outgoing<E: netemul::Endpoint>(name: &str) -> Result {
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
async fn test_drop_tcp_outgoing<E: netemul::Endpoint>(name: &str) -> Result {
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
async fn test_drop_udp_incoming_within_port_range<E: netemul::Endpoint>(name: &str) -> Result {
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
async fn test_drop_tcp_incoming_within_port_range<E: netemul::Endpoint>(name: &str) -> Result {
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
async fn test_drop_udp_outgoing_within_port_range<E: netemul::Endpoint>(name: &str) -> Result {
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
async fn test_drop_tcp_outgoing_within_port_range<E: netemul::Endpoint>(name: &str) -> Result {
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
async fn test_drop_udp_incoming_outside_port_range<E: netemul::Endpoint>(name: &str) -> Result {
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
async fn test_drop_tcp_incoming_outside_port_range<E: netemul::Endpoint>(name: &str) -> Result {
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
async fn test_drop_udp_outgoing_outside_port_range<E: netemul::Endpoint>(name: &str) -> Result {
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
async fn test_drop_tcp_outgoing_outside_port_range<E: netemul::Endpoint>(name: &str) -> Result {
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
async fn test_drop_udp_incoming_with_address_range<E: netemul::Endpoint>(name: &str) -> Result {
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
async fn test_drop_tcp_incoming_with_address_range<E: netemul::Endpoint>(name: &str) -> Result {
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
async fn test_drop_udp_outgoing_with_address_range<E: netemul::Endpoint>(name: &str) -> Result {
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
async fn test_drop_tcp_outgoing_with_address_range<E: netemul::Endpoint>(name: &str) -> Result {
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
async fn test_drop_udp_incoming_with_src_address_invert<E: netemul::Endpoint>(
    name: &str,
) -> Result {
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
async fn test_drop_tcp_incoming_with_src_address_invert<E: netemul::Endpoint>(
    name: &str,
) -> Result {
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
async fn test_drop_udp_outgoing_with_src_address_invert<E: netemul::Endpoint>(
    name: &str,
) -> Result {
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
async fn test_drop_tcp_outgoing_with_src_address_invert<E: netemul::Endpoint>(
    name: &str,
) -> Result {
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
async fn test_drop_udp_incoming_with_dst_address_invert<E: netemul::Endpoint>(
    name: &str,
) -> Result {
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
async fn test_drop_tcp_incoming_with_dst_address_invert<E: netemul::Endpoint>(
    name: &str,
) -> Result {
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
async fn test_drop_udp_outgoing_with_dst_address_invert<E: netemul::Endpoint>(
    name: &str,
) -> Result {
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
async fn test_drop_tcp_outgoing_with_dst_address_invert<E: netemul::Endpoint>(
    name: &str,
) -> Result {
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
