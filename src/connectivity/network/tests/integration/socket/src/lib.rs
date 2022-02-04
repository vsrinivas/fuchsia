// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use fuchsia_async::TimeoutExt as _;

use anyhow::Context as _;
use futures::{
    io::AsyncReadExt as _, io::AsyncWriteExt as _, FutureExt as _, StreamExt as _,
    TryFutureExt as _, TryStreamExt as _,
};
use net_declare::{fidl_ip, fidl_ip_v4, fidl_ip_v6, fidl_mac, fidl_subnet};
use netemul::{RealmTcpListener as _, RealmTcpStream as _, RealmUdpSocket as _};
use netstack_testing_common::{
    ping,
    realms::{Netstack, Netstack2, TestSandboxExt as _},
    Result,
};
use netstack_testing_macros::variants_test;
use packet::Serializer as _;
use packet_formats::{self, ipv4::Ipv4Header as _};

async fn run_udp_socket_test(
    server: &netemul::TestRealm<'_>,
    server_addr: fidl_fuchsia_net::IpAddress,
    client: &netemul::TestRealm<'_>,
    client_addr: fidl_fuchsia_net::IpAddress,
) {
    let fidl_fuchsia_net_ext::IpAddress(client_addr) =
        fidl_fuchsia_net_ext::IpAddress::from(client_addr);
    let client_addr = std::net::SocketAddr::new(client_addr, 1234);

    let fidl_fuchsia_net_ext::IpAddress(server_addr) =
        fidl_fuchsia_net_ext::IpAddress::from(server_addr);
    let server_addr = std::net::SocketAddr::new(server_addr, 8080);

    let client_sock = fuchsia_async::net::UdpSocket::bind_in_realm(client, client_addr)
        .await
        .expect("failed to create client socket");

    let server_sock = fuchsia_async::net::UdpSocket::bind_in_realm(server, server_addr)
        .await
        .expect("failed to create server socket");

    const PAYLOAD: &'static str = "Hello World";

    let client_fut = async move {
        let r = client_sock.send_to(PAYLOAD.as_bytes(), server_addr).await.expect("sendto failed");
        assert_eq!(r, PAYLOAD.as_bytes().len());
    };
    let server_fut = async move {
        let mut buf = [0u8; 1024];
        let (r, from) = server_sock.recv_from(&mut buf[..]).await.expect("recvfrom failed");
        assert_eq!(r, PAYLOAD.as_bytes().len());
        assert_eq!(&buf[..r], PAYLOAD.as_bytes());
        assert_eq!(from, client_addr);
    };

    let ((), ()) = futures::future::join(client_fut, server_fut).await;
}

const CLIENT_SUBNET: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.2/24");
const SERVER_SUBNET: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.1/24");
const CLIENT_MAC: fidl_fuchsia_net::MacAddress = fidl_mac!("02:00:00:00:00:02");
const SERVER_MAC: fidl_fuchsia_net::MacAddress = fidl_mac!("02:00:00:00:00:01");

#[variants_test]
async fn test_udp_socket<E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let net = sandbox.create_network("net").await.expect("failed to create network");

    let client = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_client", name))
        .expect("failed to create client realm");

    let client_ep = client
        .join_network_with(
            &net,
            "client",
            E::make_config(netemul::DEFAULT_MTU, Some(CLIENT_MAC)),
            &netemul::InterfaceConfig::StaticIp(CLIENT_SUBNET),
        )
        .await
        .expect("client failed to join network");
    let server = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_server", name))
        .expect("failed to create server realm");
    let server_ep = server
        .join_network_with(
            &net,
            "server",
            E::make_config(netemul::DEFAULT_MTU, Some(SERVER_MAC)),
            &netemul::InterfaceConfig::StaticIp(SERVER_SUBNET),
        )
        .await
        .expect("server failed to join network");

    // Add static ARP entries as we've observed flakes in CQ due to ARP timeouts
    // and ARP resolution is immaterial to this test.
    futures::stream::iter([
        (&server, &server_ep, CLIENT_SUBNET.addr, CLIENT_MAC),
        (&client, &client_ep, SERVER_SUBNET.addr, SERVER_MAC),
    ])
    .for_each_concurrent(None, |(realm, ep, addr, mac)| {
        realm.add_neighbor_entry(ep.id(), addr, mac).map(|r| r.expect("add_neighbor_entry"))
    })
    .await;

    run_udp_socket_test(&server, SERVER_SUBNET.addr, &client, CLIENT_SUBNET.addr).await
}

async fn run_tcp_socket_test(
    server: &netemul::TestRealm<'_>,
    server_addr: fidl_fuchsia_net::IpAddress,
    client: &netemul::TestRealm<'_>,
    client_addr: fidl_fuchsia_net::IpAddress,
) {
    let fidl_fuchsia_net_ext::IpAddress(client_addr) = client_addr.into();
    let client_addr = std::net::SocketAddr::new(client_addr, 1234);

    let fidl_fuchsia_net_ext::IpAddress(server_addr) = server_addr.into();
    let server_addr = std::net::SocketAddr::new(server_addr, 8080);

    // We pick a payload that is small enough to be guaranteed to fit in a TCP segment so both the
    // client and server can read the entire payload in a single `read`.
    const PAYLOAD: &'static str = "Hello World";

    let listener = fuchsia_async::net::TcpListener::listen_in_realm(server, server_addr)
        .await
        .expect("failed to create server socket");

    let server_fut = async {
        let (_, mut stream, from) = listener.accept().await.expect("accept failed");

        let mut buf = [0u8; 1024];
        let read_count = stream.read(&mut buf).await.expect("read from tcp server stream failed");

        assert_eq!(from.ip(), client_addr.ip());
        assert_eq!(read_count, PAYLOAD.as_bytes().len());
        assert_eq!(&buf[..read_count], PAYLOAD.as_bytes());

        let write_count =
            stream.write(PAYLOAD.as_bytes()).await.expect("write to tcp server stream failed");
        assert_eq!(write_count, PAYLOAD.as_bytes().len());
    };

    let client_fut = async {
        let mut stream = fuchsia_async::net::TcpStream::connect_in_realm(client, server_addr)
            .await
            .expect("failed to create client socket");

        let write_count =
            stream.write(PAYLOAD.as_bytes()).await.expect("write to tcp client stream failed");

        assert_eq!(write_count, PAYLOAD.as_bytes().len());

        let mut buf = [0u8; 1024];
        let read_count = stream.read(&mut buf).await.expect("read from tcp client stream failed");

        assert_eq!(read_count, PAYLOAD.as_bytes().len());
        assert_eq!(&buf[..read_count], PAYLOAD.as_bytes());
    };

    let ((), ()) = futures::future::join(client_fut, server_fut).await;
}

#[variants_test]
async fn test_tcp_socket<E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let net = sandbox.create_network("net").await.expect("failed to create network");

    let client = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_client", name))
        .expect("failed to create client realm");
    let client_ep = client
        .join_network_with(
            &net,
            "client",
            E::make_config(netemul::DEFAULT_MTU, Some(CLIENT_MAC)),
            &netemul::InterfaceConfig::StaticIp(CLIENT_SUBNET),
        )
        .await
        .expect("client failed to join network");

    let server = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_client", name))
        .expect("failed to create server realm");
    let server_ep = server
        .join_network_with(
            &net,
            "server",
            E::make_config(netemul::DEFAULT_MTU, Some(SERVER_MAC)),
            &netemul::InterfaceConfig::StaticIp(SERVER_SUBNET),
        )
        .await
        .expect("server failed to join network");

    // Add static ARP entries as we've observed flakes in CQ due to ARP timeouts
    // and ARP resolution is immaterial to this test.
    futures::stream::iter([
        (&server, &server_ep, CLIENT_SUBNET.addr, CLIENT_MAC),
        (&client, &client_ep, SERVER_SUBNET.addr, SERVER_MAC),
    ])
    .for_each_concurrent(None, |(realm, ep, addr, mac)| {
        realm.add_neighbor_entry(ep.id(), addr, mac).map(|r| r.expect("add_neighbor_entry"))
    })
    .await;

    run_tcp_socket_test(&server, SERVER_SUBNET.addr, &client, CLIENT_SUBNET.addr).await
}

// Helper function to add ip device to stack.
async fn install_ip_device(
    realm: &netemul::TestRealm<'_>,
    port: fidl_fuchsia_hardware_network::PortProxy,
    addrs: impl IntoIterator<Item = fidl_fuchsia_net::Subnet>,
) -> (
    u64,
    fidl_fuchsia_net_interfaces_ext::admin::Control,
    fidl_fuchsia_net_interfaces_admin::DeviceControlProxy,
) {
    let installer =
        realm.connect_to_protocol::<fidl_fuchsia_net_interfaces_admin::InstallerMarker>().unwrap();
    let stack = realm.connect_to_protocol::<fidl_fuchsia_net_stack::StackMarker>().unwrap();

    let mut port_id = port.get_info().await.expect("get port info").id.expect("missing port id");
    let device = {
        let (device, server_end) =
            fidl::endpoints::create_endpoints::<fidl_fuchsia_hardware_network::DeviceMarker>()
                .expect("create endpoints");
        let () = port.get_device(server_end).expect("get device");
        device
    };
    let device_control = {
        let (control, server_end) = fidl::endpoints::create_proxy::<
            fidl_fuchsia_net_interfaces_admin::DeviceControlMarker,
        >()
        .expect("create proxy");
        let () = installer.install_device(device, server_end).expect("install device");
        control
    };
    let control = {
        let (control, server_end) =
            fidl_fuchsia_net_interfaces_ext::admin::Control::create_endpoints()
                .expect("create endpoints");
        let () = device_control
            .create_interface(
                &mut port_id,
                server_end,
                fidl_fuchsia_net_interfaces_admin::Options::EMPTY,
            )
            .expect("create interface");
        control
    };
    assert!(control.enable().await.expect("enable interface").expect("failed to enable interface"));

    let id = control.get_id().await.expect("get id");

    let () = futures::stream::iter(addrs.into_iter())
        .for_each_concurrent(None, |subnet| {
            let (address_state_provider, server_end) = fidl::endpoints::create_proxy::<
                fidl_fuchsia_net_interfaces_admin::AddressStateProviderMarker,
            >()
            .expect("create proxy");
            let fidl_fuchsia_net::Subnet { addr, prefix_len } = &subnet;
            let mut interface_address = match addr {
                fidl_fuchsia_net::IpAddress::Ipv4(a) => fidl_fuchsia_net::InterfaceAddress::Ipv4(
                    fidl_fuchsia_net::Ipv4AddressWithPrefix {
                        addr: a.clone(),
                        prefix_len: *prefix_len,
                    },
                ),
                fidl_fuchsia_net::IpAddress::Ipv6(a) => {
                    fidl_fuchsia_net::InterfaceAddress::Ipv6(a.clone())
                }
            };

            // We're not interested in maintaining the address' lifecycle through
            // the proxy.
            let () = address_state_provider.detach().expect("detach");
            let () = control
                .add_address(
                    &mut interface_address,
                    fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
                    server_end,
                )
                .expect("add address");

            // Wait for the address to be assigned.
            let wait_assignment_fut =
                fidl_fuchsia_net_interfaces_ext::admin::wait_assignment_state(
                    fidl_fuchsia_net_interfaces_ext::admin::assignment_state_stream(
                        address_state_provider,
                    ),
                    fidl_fuchsia_net_interfaces_admin::AddressAssignmentState::Assigned,
                )
                .map(|r| r.expect("wait assignment state"));

            // NB: add_address above does NOT create a subnet route.
            let add_forwarding_entry_fut = stack
                .add_forwarding_entry(&mut fidl_fuchsia_net_stack::ForwardingEntry {
                    subnet: fidl_fuchsia_net_ext::apply_subnet_mask(subnet.clone()),
                    device_id: id,
                    next_hop: None,
                    metric: 0,
                })
                .map(move |r| {
                    r.squash_result().unwrap_or_else(|e| {
                        panic!("failed to add interface address {:?}: {:?}", subnet, e)
                    })
                });
            futures::future::join(wait_assignment_fut, add_forwarding_entry_fut).map(|((), ())| ())
        })
        .await;
    (id, control, device_control)
}

const TUN_DEFAULT_PORT_ID: u8 = 0;

/// Creates default base config for an IP tun device.
fn base_ip_device_port_config() -> fidl_fuchsia_net_tun::BasePortConfig {
    fidl_fuchsia_net_tun::BasePortConfig {
        id: Some(TUN_DEFAULT_PORT_ID),
        mtu: Some(1500),
        rx_types: Some(vec![
            fidl_fuchsia_hardware_network::FrameType::Ipv4,
            fidl_fuchsia_hardware_network::FrameType::Ipv6,
        ]),
        tx_types: Some(vec![
            fidl_fuchsia_hardware_network::FrameTypeSupport {
                type_: fidl_fuchsia_hardware_network::FrameType::Ipv4,
                features: fidl_fuchsia_hardware_network::FRAME_FEATURES_RAW,
                supported_flags: fidl_fuchsia_hardware_network::TxFlags::empty(),
            },
            fidl_fuchsia_hardware_network::FrameTypeSupport {
                type_: fidl_fuchsia_hardware_network::FrameType::Ipv6,
                features: fidl_fuchsia_hardware_network::FRAME_FEATURES_RAW,
                supported_flags: fidl_fuchsia_hardware_network::TxFlags::empty(),
            },
        ]),
        ..fidl_fuchsia_net_tun::BasePortConfig::EMPTY
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_ip_endpoints_socket() {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let client = sandbox
        .create_netstack_realm::<Netstack2, _>("test_ip_endpoints_socket_client")
        .expect("failed to create client realm");
    let server = sandbox
        .create_netstack_realm::<Netstack2, _>("test_ip_endpoints_socket_server")
        .expect("failed to create server realm");

    let tun =
        fuchsia_component::client::connect_to_protocol::<fidl_fuchsia_net_tun::ControlMarker>()
            .expect("failed to connect to tun protocol");

    let (tun_pair, req) = fidl::endpoints::create_proxy::<fidl_fuchsia_net_tun::DevicePairMarker>()
        .expect("failed to create endpoints");
    let () = tun
        .create_pair(fidl_fuchsia_net_tun::DevicePairConfig::EMPTY, req)
        .expect("failed to create tun pair");

    let () = tun_pair
        .add_port(fidl_fuchsia_net_tun::DevicePairPortConfig {
            base: Some(base_ip_device_port_config()),
            // No MAC, this is a pure IP device.
            mac_left: None,
            mac_right: None,
            ..fidl_fuchsia_net_tun::DevicePairPortConfig::EMPTY
        })
        .await
        .expect("add_port failed")
        .map_err(fuchsia_zircon::Status::from_raw)
        .expect("add_port returned error");

    let (client_port, client_req) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_hardware_network::PortMarker>()
            .expect("failed to create proxy");
    let (server_port, server_req) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_hardware_network::PortMarker>()
            .expect("failed to create proxy");
    let () = tun_pair.get_left_port(TUN_DEFAULT_PORT_ID, client_req).expect("get_left failed");
    let () = tun_pair.get_right_port(TUN_DEFAULT_PORT_ID, server_req).expect("get_right failed");

    // Addresses must be in the same subnet.
    const SERVER_ADDR_V4: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.1/24");
    const SERVER_ADDR_V6: fidl_fuchsia_net::Subnet = fidl_subnet!("2001::1/120");
    const CLIENT_ADDR_V4: fidl_fuchsia_net::Subnet = fidl_subnet!("192.168.0.2/24");
    const CLIENT_ADDR_V6: fidl_fuchsia_net::Subnet = fidl_subnet!("2001::2/120");

    // We install both devices in parallel because a DevicePair will only have
    // its link signal set to up once both sides have sessions attached. This
    // way both devices will be configured "at the same time" and DAD will be
    // able to complete for IPv6 addresses.
    let (
        (_client_id, _client_control, _client_device_control),
        (_server_id, _server_control, _server_device_control),
    ) = futures::future::join(
        install_ip_device(&client, client_port, [CLIENT_ADDR_V4, CLIENT_ADDR_V6]),
        install_ip_device(&server, server_port, [SERVER_ADDR_V4, SERVER_ADDR_V6]),
    )
    .await;

    // Run socket test for both IPv4 and IPv6.
    let () = run_udp_socket_test(&server, SERVER_ADDR_V4.addr, &client, CLIENT_ADDR_V4.addr).await;
    let () = run_udp_socket_test(&server, SERVER_ADDR_V6.addr, &client, CLIENT_ADDR_V6.addr).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_ip_endpoint_packets() {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let realm = sandbox
        .create_netstack_realm::<Netstack2, _>("test_ip_endpoint_packets")
        .expect("failed to create client realm");

    let tun =
        fuchsia_component::client::connect_to_protocol::<fidl_fuchsia_net_tun::ControlMarker>()
            .expect("failed to connect to tun protocol");

    let (tun_dev, req) = fidl::endpoints::create_proxy::<fidl_fuchsia_net_tun::DeviceMarker>()
        .expect("failed to create endpoints");
    let () = tun
        .create_device(
            fidl_fuchsia_net_tun::DeviceConfig {
                base: None,
                blocking: Some(true),
                ..fidl_fuchsia_net_tun::DeviceConfig::EMPTY
            },
            req,
        )
        .expect("failed to create tun pair");

    let (_tun_port, port) = {
        let (tun_port, server_end) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_tun::PortMarker>()
                .expect("failed to create endpoints");
        let () = tun_dev
            .add_port(
                fidl_fuchsia_net_tun::DevicePortConfig {
                    base: Some(base_ip_device_port_config()),
                    online: Some(true),
                    // No MAC, this is a pure IP device.
                    mac: None,
                    ..fidl_fuchsia_net_tun::DevicePortConfig::EMPTY
                },
                server_end,
            )
            .expect("add_port failed");

        let (port, server_end) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_hardware_network::PortMarker>()
                .expect("failed to create endpoints");
        let () = tun_port.get_port(server_end).expect("get_port failed");
        (tun_port, port)
    };

    // Declare addresses in the same subnet. Alice is Netstack, and Bob is our
    // end of the tun device that we'll use to inject frames.
    const PREFIX_V4: u8 = 24;
    const PREFIX_V6: u8 = 120;
    const ALICE_ADDR_V4: fidl_fuchsia_net::Ipv4Address = fidl_ip_v4!("192.168.0.1");
    const ALICE_ADDR_V6: fidl_fuchsia_net::Ipv6Address = fidl_ip_v6!("2001::1");
    const BOB_ADDR_V4: fidl_fuchsia_net::Ipv4Address = fidl_ip_v4!("192.168.0.2");
    const BOB_ADDR_V6: fidl_fuchsia_net::Ipv6Address = fidl_ip_v6!("2001::2");

    let (_id, _control, _device_control) = install_ip_device(
        &realm,
        port,
        [
            fidl_fuchsia_net::Subnet {
                addr: fidl_fuchsia_net::IpAddress::Ipv4(ALICE_ADDR_V4),
                prefix_len: PREFIX_V4,
            },
            fidl_fuchsia_net::Subnet {
                addr: fidl_fuchsia_net::IpAddress::Ipv6(ALICE_ADDR_V6),
                prefix_len: PREFIX_V6,
            },
        ],
    )
    .await;

    use net_types::ip::{Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
    use packet::ParsablePacket;
    use packet_formats::{
        icmp::{
            IcmpEchoRequest, IcmpPacketBuilder, IcmpUnusedCode, Icmpv4Packet, Icmpv6Packet,
            MessageBody,
        },
        igmp::messages::IgmpPacket,
        ipv4::{Ipv4Packet, Ipv4PacketBuilder},
        ipv6::{Ipv6Header, Ipv6Packet, Ipv6PacketBuilder},
    };

    let read_frame = futures::stream::try_unfold(tun_dev.clone(), |tun_dev| async move {
        let frame = tun_dev
            .read_frame()
            .await
            .context("read_frame_failed")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("read_frame returned error")?;
        Ok(Some((frame, tun_dev)))
    })
    .try_filter_map(|frame| async move {
        let frame_type = frame.frame_type.context("missing frame type in frame")?;
        let frame_data = frame.data.context("missing data in frame")?;
        match frame_type {
            fidl_fuchsia_hardware_network::FrameType::Ipv6 => {
                // Ignore all NDP and MLD IPv6 frames.
                let mut bv = &frame_data[..];
                let ipv6 = Ipv6Packet::parse(&mut bv, ())
                    .with_context(|| format!("failed to parse IPv6 packet {:?}", frame_data))?;
                if ipv6.proto() == packet_formats::ip::Ipv6Proto::Icmpv6 {
                    let parse_args =
                        packet_formats::icmp::IcmpParseArgs::new(ipv6.src_ip(), ipv6.dst_ip());
                    match Icmpv6Packet::parse(&mut bv, parse_args)
                        .context("failed to parse ICMP packet")?
                    {
                        Icmpv6Packet::Ndp(p) => {
                            println!("ignoring NDP packet {:?}", p);
                            return Ok(None);
                        }
                        Icmpv6Packet::Mld(p) => {
                            println!("ignoring MLD packet {:?}", p);
                            return Ok(None);
                        }
                        Icmpv6Packet::DestUnreachable(_)
                        | Icmpv6Packet::PacketTooBig(_)
                        | Icmpv6Packet::TimeExceeded(_)
                        | Icmpv6Packet::ParameterProblem(_)
                        | Icmpv6Packet::EchoRequest(_)
                        | Icmpv6Packet::EchoReply(_) => {}
                    }
                }
            }
            fidl_fuchsia_hardware_network::FrameType::Ipv4 => {
                // Ignore all IGMP frames.
                let mut bv = &frame_data[..];
                let ipv4 = Ipv4Packet::parse(&mut bv, ())
                    .with_context(|| format!("failed to parse IPv4 packet {:?}", frame_data))?;
                if ipv4.proto() == packet_formats::ip::Ipv4Proto::Igmp {
                    let p =
                        IgmpPacket::parse(&mut bv, ()).context("failed to parse IGMP packet")?;
                    println!("ignoring IGMP packet {:?}", p);
                    return Ok(None);
                }
            }
            fidl_fuchsia_hardware_network::FrameType::Ethernet => {}
        }
        Ok(Some((frame_type, frame_data)))
    });
    futures::pin_mut!(read_frame);

    async fn write_frame_and_read_with_timeout<S>(
        tun_dev: &fidl_fuchsia_net_tun::DeviceProxy,
        frame: fidl_fuchsia_net_tun::Frame,
        read_frame: &mut S,
    ) -> Result<Option<S::Ok>>
    where
        S: futures::stream::TryStream<Error = anyhow::Error> + std::marker::Unpin,
    {
        let () = tun_dev
            .write_frame(frame)
            .await
            .context("write_frame failed")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("write_frame returned error")?;
        Ok(read_frame
            .try_next()
            .and_then(|f| {
                futures::future::ready(f.context("frame stream ended unexpectedly").map(Some))
            })
            .on_timeout(
                fuchsia_async::Time::after(fuchsia_zircon::Duration::from_millis(50)),
                || Ok(None),
            )
            .await
            .context("failed to read frame")?)
    }

    const ICMP_ID: u16 = 10;
    const SEQ_NUM: u16 = 1;
    let mut payload = [1u8, 2, 3, 4];

    // Manually build a ping frame and see it come back out of the stack.
    let src_ip = Ipv4Addr::new(BOB_ADDR_V4.addr);
    let dst_ip = Ipv4Addr::new(ALICE_ADDR_V4.addr);
    let packet = packet::Buf::new(&mut payload[..], ..)
        .encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
            src_ip,
            dst_ip,
            IcmpUnusedCode,
            IcmpEchoRequest::new(ICMP_ID, SEQ_NUM),
        ))
        .encapsulate(Ipv4PacketBuilder::new(src_ip, dst_ip, 1, packet_formats::ip::Ipv4Proto::Icmp))
        .serialize_vec_outer()
        .expect("serialization failed")
        .as_ref()
        .to_vec();

    // Send v4 ping request.
    let () = tun_dev
        .write_frame(fidl_fuchsia_net_tun::Frame {
            port: Some(TUN_DEFAULT_PORT_ID),
            frame_type: Some(fidl_fuchsia_hardware_network::FrameType::Ipv4),
            data: Some(packet.clone()),
            meta: None,
            ..fidl_fuchsia_net_tun::Frame::EMPTY
        })
        .await
        .expect("write_frame failed")
        .map_err(fuchsia_zircon::Status::from_raw)
        .expect("write_frame returned error");

    // Read ping response.
    let (frame_type, data) = read_frame
        .try_next()
        .await
        .expect("failed to read ping response")
        .expect("frame stream ended unexpectedly");
    assert_eq!(frame_type, fidl_fuchsia_hardware_network::FrameType::Ipv4);
    let mut bv = &data[..];
    let ipv4_packet = Ipv4Packet::parse(&mut bv, ()).expect("failed to parse IPv4 packet");
    assert_eq!(ipv4_packet.src_ip(), dst_ip);
    assert_eq!(ipv4_packet.dst_ip(), src_ip);
    assert_eq!(ipv4_packet.proto(), packet_formats::ip::Ipv4Proto::Icmp);

    let parse_args =
        packet_formats::icmp::IcmpParseArgs::new(ipv4_packet.src_ip(), ipv4_packet.dst_ip());
    let icmp_packet =
        match Icmpv4Packet::parse(&mut bv, parse_args).expect("failed to parse ICMP packet") {
            Icmpv4Packet::EchoReply(reply) => reply,
            p => panic!("got ICMP packet {:?}, want EchoReply", p),
        };
    assert_eq!(icmp_packet.message().id(), ICMP_ID);
    assert_eq!(icmp_packet.message().seq(), SEQ_NUM);
    assert_eq!(icmp_packet.body().bytes(), &payload[..]);

    // Send the same data again, but with an IPv6 frame type, expect that it'll
    // fail parsing and no response will be generated.
    assert_matches::assert_matches!(
        write_frame_and_read_with_timeout(
            &tun_dev,
            fidl_fuchsia_net_tun::Frame {
                port: Some(TUN_DEFAULT_PORT_ID),
                frame_type: Some(fidl_fuchsia_hardware_network::FrameType::Ipv6),
                data: Some(packet),
                meta: None,
                ..fidl_fuchsia_net_tun::Frame::EMPTY
            },
            &mut read_frame,
        )
        .await,
        Ok(None)
    );

    // Manually build a V6 ping frame and see it come back out of the stack.
    let src_ip = Ipv6Addr::from_bytes(BOB_ADDR_V6.addr);
    let dst_ip = Ipv6Addr::from_bytes(ALICE_ADDR_V6.addr);
    let packet = packet::Buf::new(&mut payload[..], ..)
        .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
            src_ip,
            dst_ip,
            IcmpUnusedCode,
            IcmpEchoRequest::new(ICMP_ID, SEQ_NUM),
        ))
        .encapsulate(Ipv6PacketBuilder::new(
            src_ip,
            dst_ip,
            1,
            packet_formats::ip::Ipv6Proto::Icmpv6,
        ))
        .serialize_vec_outer()
        .expect("serialization failed")
        .as_ref()
        .to_vec();

    // Send v6 ping request.
    let () = tun_dev
        .write_frame(fidl_fuchsia_net_tun::Frame {
            port: Some(TUN_DEFAULT_PORT_ID),
            frame_type: Some(fidl_fuchsia_hardware_network::FrameType::Ipv6),
            data: Some(packet.clone()),
            meta: None,
            ..fidl_fuchsia_net_tun::Frame::EMPTY
        })
        .await
        .expect("write_frame failed")
        .map_err(fuchsia_zircon::Status::from_raw)
        .expect("write_frame returned error");

    // Read ping response.
    let (frame_type, data) = read_frame
        .try_next()
        .await
        .expect("failed to read ping response")
        .expect("frame stream ended unexpectedly");
    assert_eq!(frame_type, fidl_fuchsia_hardware_network::FrameType::Ipv6);
    let mut bv = &data[..];
    let ipv6_packet = Ipv6Packet::parse(&mut bv, ()).expect("failed to parse IPv6 packet");
    assert_eq!(ipv6_packet.src_ip(), dst_ip);
    assert_eq!(ipv6_packet.dst_ip(), src_ip);
    assert_eq!(ipv6_packet.proto(), packet_formats::ip::Ipv6Proto::Icmpv6);

    let parse_args =
        packet_formats::icmp::IcmpParseArgs::new(ipv6_packet.src_ip(), ipv6_packet.dst_ip());
    let icmp_packet =
        match Icmpv6Packet::parse(&mut bv, parse_args).expect("failed to parse ICMPv6 packet") {
            Icmpv6Packet::EchoReply(reply) => reply,
            p => panic!("got ICMPv6 packet {:?}, want EchoReply", p),
        };
    assert_eq!(icmp_packet.message().id(), ICMP_ID);
    assert_eq!(icmp_packet.message().seq(), SEQ_NUM);
    assert_eq!(icmp_packet.body().bytes(), &payload[..]);

    // Send the same data again, but with an IPv4 frame type, expect that it'll
    // fail parsing and no response will be generated.
    assert_matches::assert_matches!(
        write_frame_and_read_with_timeout(
            &tun_dev,
            fidl_fuchsia_net_tun::Frame {
                port: Some(TUN_DEFAULT_PORT_ID),
                frame_type: Some(fidl_fuchsia_hardware_network::FrameType::Ipv4),
                data: Some(packet),
                meta: None,
                ..fidl_fuchsia_net_tun::Frame::EMPTY
            },
            &mut read_frame,
        )
        .await,
        Ok(None)
    );
}

#[variants_test]
async fn ping<E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let net = sandbox.create_network("net").await.expect("failed to create network");

    let create_realm = |suffix, addr| {
        let sandbox = &sandbox;
        let net = &net;
        async move {
            let realm = sandbox
                .create_netstack_realm::<Netstack2, _>(format!("{}_{}", name, suffix))
                .expect("failed to create realm");
            let interface = realm
                .join_network::<E, _>(
                    &net,
                    format!("ep_{}", suffix),
                    &netemul::InterfaceConfig::StaticIp(addr),
                )
                .await
                .expect("failed to join network in realm");
            (realm, interface)
        }
    };

    let (realm_a, if_a) = create_realm("a", fidl_subnet!("192.168.1.1/16")).await;
    let (realm_b, if_b) = create_realm("b", fidl_subnet!("192.168.1.2/16")).await;

    let node_a = ping::Node::new_with_v4_and_v6_link_local(&realm_a, &if_a)
        .await
        .expect("failed to construct node A");
    let node_b = ping::Node::new_with_v4_and_v6_link_local(&realm_b, &if_b)
        .await
        .expect("failed to construct node B");

    node_a
        .ping_pairwise(std::slice::from_ref(&node_b))
        .await
        .expect("failed to ping between nodes");
}

#[variants_test]
async fn udpv4_loopback<N: Netstack>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("failed to create realm");

    const IPV4_LOOPBACK: fidl_fuchsia_net::IpAddress = fidl_ip!("127.0.0.1");
    run_udp_socket_test(&realm, IPV4_LOOPBACK, &realm, IPV4_LOOPBACK).await
}

#[variants_test]
async fn udpv6_loopback<N: Netstack>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("failed to create realm");

    const IPV6_LOOPBACK: fidl_fuchsia_net::IpAddress = fidl_ip!("::1");
    run_udp_socket_test(&realm, IPV6_LOOPBACK, &realm, IPV6_LOOPBACK).await
}
